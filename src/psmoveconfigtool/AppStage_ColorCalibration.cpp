//-- inludes -----
#include "AppStage_ColorCalibration.h"
#include "AppStage_TrackerSettings.h"
#include "AppStage_MainMenu.h"
#include "AssetManager.h"
#include "App.h"
#include "Camera.h"
#include "ClientLog.h"
#include "MathUtility.h"
#include "Renderer.h"
#include "UIConstants.h"
#include "PSMoveProtocolInterface.h"
#include "PSMoveProtocol.pb.h"
#include "SharedTrackerState.h"

#include "SDL_keycode.h"
#include "SDL_opengl.h"

#include <imgui.h>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning (disable: 4996) // 'This function or variable may be unsafe': snprintf
#define snprintf _snprintf
#endif

//-- statics ----
const char *AppStage_ColorCalibration::APP_STAGE_NAME = "ColorCalibration";

//-- constants -----
static const char *k_video_display_mode_names[] = {
    "BGR",
    "HSV",
    "HSV Range"
};

//-- private methods -----

//-- public methods -----
AppStage_ColorCalibration::AppStage_ColorCalibration(App *app)
    : AppStage(app)
    , m_menuState(AppStage_ColorCalibration::inactive)
    , m_bStreamIsActive(false)
    , m_trackerView(nullptr)
    , m_videoTexture(nullptr)
    , m_videoDisplayMode(AppStage_ColorCalibration::eVideoDisplayMode::mode_bgr)
    , m_trackerExposure(0)
    , m_trackerGain(0)
{ }

void AppStage_ColorCalibration::enter()
{
    const AppStage_TrackerSettings *trackerSettings =
        m_app->getAppStage<AppStage_TrackerSettings>();
    const ClientTrackerInfo *trackerInfo = trackerSettings->getSelectedTrackerInfo();
    assert(trackerInfo->tracker_id != -1);

    m_app->setCameraType(_cameraFixed);

    assert(m_trackerView == nullptr);
    m_trackerView = ClientPSMoveAPI::allocate_tracker_view(*trackerInfo);

    request_tracker_get_settings(m_trackerView->getTrackerId());

    assert(!m_bStreamIsActive);
    request_tracker_start_stream(m_trackerView->getTrackerId());
}

void AppStage_ColorCalibration::exit()
{
    m_menuState = AppStage_ColorCalibration::inactive;

    ClientPSMoveAPI::free_tracker_view(m_trackerView);
    m_trackerView = nullptr;
}

void AppStage_ColorCalibration::update()
{
    // Try and read the next video frame from shared memory
    if (m_videoTexture != nullptr)
    {
        if (m_trackerView->pollVideoStream())
        {
            m_videoTexture->copyBufferIntoTexture(m_trackerView->getVideoFrameBuffer());
        }
    }
}

void AppStage_ColorCalibration::render()
{
    // If there is a video frame available to render, show it
    if (m_videoTexture != nullptr)
    {
        unsigned int texture_id = m_videoTexture->texture_id;

        if (texture_id != 0)
        {
            drawFullscreenTexture(texture_id);
        }
    }
}

void AppStage_ColorCalibration::renderUI()
{
    const AppStage_TrackerSettings *trackerSettings = m_app->getAppStage<AppStage_TrackerSettings>();
    const ClientTrackerInfo *trackerInfo = trackerSettings->getSelectedTrackerInfo();

    const float k_panel_width = 300.f;
    const char *k_window_title = "Color Calibration";
    const ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_ShowBorders |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse;

    switch (m_menuState)
    {
    case eTrackerMenuState::idle:
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2.f - k_panel_width / 2.f, 20.f));
        ImGui::SetNextWindowSize(ImVec2(k_panel_width, 200));
        ImGui::Begin(k_window_title, nullptr, window_flags);

        if (ImGui::Button("Return to Tracker Settings"))
        {
            if (m_bStreamIsActive)
            {
                request_tracker_stop_stream(trackerInfo->tracker_id);
            }
            else
            {
                m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
            }
        }

        if (m_bStreamIsActive)
        {
            if (ImGui::Button("<##Filter"))
            {
                m_videoDisplayMode = 
                    static_cast<eVideoDisplayMode>(
                        (m_videoDisplayMode + eVideoDisplayMode::MAX_VIDEO_DISPLAY_MODES - 1) 
                        % eVideoDisplayMode::MAX_VIDEO_DISPLAY_MODES);
            }
            ImGui::SameLine();
            if (ImGui::Button(">##Filter"))
            {
                m_videoDisplayMode =
                    static_cast<eVideoDisplayMode>(
                    (m_videoDisplayMode + 1) % eVideoDisplayMode::MAX_VIDEO_DISPLAY_MODES);
            }
            ImGui::SameLine();
            ImGui::Text("Video Filter Mode: %s", k_video_display_mode_names[m_videoDisplayMode]);

            if (ImGui::Button("+##Exposure"))
            {
                request_tracker_set_exposure(trackerInfo->tracker_id, m_trackerExposure + 8);
            }
            ImGui::SameLine();
            if (ImGui::Button("-##Exposure"))
            {
                request_tracker_set_exposure(trackerInfo->tracker_id, m_trackerExposure - 8);
            }
            ImGui::SameLine();
            ImGui::Text("Exposure: %f", m_trackerExposure);

            if (ImGui::Button("+##Gain"))
            {
                request_tracker_set_gain(trackerInfo->tracker_id, m_trackerGain + 8);
            }
            ImGui::SameLine();
            if (ImGui::Button("-##Gain"))
            {
                request_tracker_set_gain(trackerInfo->tracker_id, m_trackerGain - 8);
            }
            ImGui::SameLine();
            ImGui::Text("Gain: %f", m_trackerGain);

            // Render all of the option sets fetched from the settings query
            for (auto it = m_trackerOptions.begin(); it != m_trackerOptions.end(); ++it)
            {
                TrackerOption &option = *it;
                const int value_count = static_cast<int>(option.option_strings.size());

                ImGui::PushID(option.option_name.c_str());
                if (ImGui::Button("<"))
                {
                    request_tracker_set_option(
                        trackerInfo->tracker_id, 
                        option, 
                        (option.option_index + value_count - 1) % value_count);
                }
                ImGui::SameLine();
                if (ImGui::Button(">"))
                {
                    request_tracker_set_option(
                        trackerInfo->tracker_id,
                        option,
                        (option.option_index + 1) % value_count);
                }
                ImGui::SameLine();
                ImGui::Text("%s: %s", option.option_name.c_str(), option.option_strings[option.option_index].c_str());
                ImGui::PopID();
            }
        }

        ImGui::End();
    } break;

    case eTrackerMenuState::pendingTrackerStartStreamRequest:
    {
        ImGui::SetNextWindowPosCenter();
        ImGui::SetNextWindowSize(ImVec2(k_panel_width, 50));
        ImGui::Begin(k_window_title, nullptr, window_flags);

        ImGui::Text("Waiting for tracker stream to start...");

        ImGui::End();
    } break;

    case eTrackerMenuState::failedTrackerStartStreamRequest:
    {
        ImGui::SetNextWindowPosCenter();
        ImGui::SetNextWindowSize(ImVec2(k_panel_width, 130));
        ImGui::Begin(k_window_title, nullptr, window_flags);

        ImGui::Text("Failed to start tracker stream!");

        if (ImGui::Button("Ok"))
        {
            m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
        }

        if (ImGui::Button("Return to Main Menu"))
        {
            m_app->setAppStage(AppStage_MainMenu::APP_STAGE_NAME);
        }

        ImGui::End();
    } break;

    case eTrackerMenuState::pendingTrackerStopStreamRequest:
    {
        ImGui::SetNextWindowPosCenter();
        ImGui::SetNextWindowSize(ImVec2(k_panel_width, 50));
        ImGui::Begin(k_window_title, nullptr, window_flags);

        ImGui::Text("Waiting for tracker stream to stop...");

        ImGui::End();
    } break;

    case eTrackerMenuState::failedTrackerStopStreamRequest:
    {
        ImGui::SetNextWindowPosCenter();
        ImGui::SetNextWindowSize(ImVec2(k_panel_width, 130));
        ImGui::Begin(k_window_title, nullptr, window_flags);

        ImGui::Text("Failed to stop tracker stream!");

        if (ImGui::Button("Ok"))
        {
            m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
        }

        if (ImGui::Button("Return to Main Menu"))
        {
            m_app->setAppStage(AppStage_MainMenu::APP_STAGE_NAME);
        }

        ImGui::End();
    } break;

    default:
        assert(0 && "unreachable");
    }
}

void AppStage_ColorCalibration::request_tracker_start_stream(
    int trackerID)
{
    if (m_menuState != AppStage_ColorCalibration::pendingTrackerStartStreamRequest)
    {
        m_menuState = AppStage_ColorCalibration::pendingTrackerStartStreamRequest;

        // Tell the psmove service that we want to start streaming data from the tracker
        ClientPSMoveAPI::register_callback(
            ClientPSMoveAPI::start_tracker_data_stream(m_trackerView),
            AppStage_ColorCalibration::handle_tracker_start_stream_response, this);
    }
}

void AppStage_ColorCalibration::handle_tracker_start_stream_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    AppStage_ColorCalibration *thisPtr = static_cast<AppStage_ColorCalibration *>(userdata);

    switch (response->result_code)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            ClientTrackerView *trackerView = thisPtr->m_trackerView;

            thisPtr->m_bStreamIsActive = true;
            thisPtr->m_menuState = AppStage_ColorCalibration::idle;

            // Open the shared memory that the vidoe stream is being written to
            if (trackerView->openVideoStream())
            {
                // Create a texture to render the video frame to
                thisPtr->m_videoTexture = new TextureAsset();
                thisPtr->m_videoTexture->init(
                    trackerView->getVideoFrameWidth(),
                    trackerView->getVideoFrameHeight(),
                    GL_RGB, // texture format
                    GL_BGR, // buffer format
                    nullptr);
            }

            // Get the tracker settings now that the tracker stream is open
            thisPtr->request_tracker_get_settings(thisPtr->m_trackerView->getTrackerId());
        } break;

    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            thisPtr->m_menuState = AppStage_ColorCalibration::failedTrackerStartStreamRequest;
        } break;
    }
}

void AppStage_ColorCalibration::request_tracker_stop_stream(
    int trackerID)
{
    if (m_bStreamIsActive && m_menuState != AppStage_ColorCalibration::pendingTrackerStopStreamRequest)
    {
        m_menuState = AppStage_ColorCalibration::pendingTrackerStopStreamRequest;

        // Tell the psmove service that we want to stop streaming data from the tracker        
        ClientPSMoveAPI::register_callback(
            ClientPSMoveAPI::stop_tracker_data_stream(m_trackerView),
            AppStage_ColorCalibration::handle_tracker_stop_stream_response, this);
    }
}

void AppStage_ColorCalibration::handle_tracker_stop_stream_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    AppStage_ColorCalibration *thisPtr = static_cast<AppStage_ColorCalibration *>(userdata);

    // In either case consider the stream as now inactive
    thisPtr->m_bStreamIsActive = false;

    switch (response->result_code)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            thisPtr->m_menuState = AppStage_ColorCalibration::inactive;

            // Close the shared memory buffer
            thisPtr->m_trackerView->closeVideoStream();

            // Free the texture we were rendering to
            if (thisPtr->m_videoTexture != nullptr)
            {
                delete thisPtr->m_videoTexture;
                thisPtr->m_videoTexture = nullptr;
            }

            // After closing the stream, we should go back to the tracker settings
            thisPtr->m_app->setAppStage(AppStage_TrackerSettings::APP_STAGE_NAME);
        } break;

    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            thisPtr->m_menuState = AppStage_ColorCalibration::failedTrackerStopStreamRequest;
        } break;
    }
}

void AppStage_ColorCalibration::request_tracker_set_exposure(int trackerID, double value)
{
    // Tell the psmove service that we want to change exposure.
    RequestPtr request(new PSMoveProtocol::Request());
    request->set_type(PSMoveProtocol::Request_RequestType_SET_TRACKER_EXPOSURE);
    request->mutable_request_set_tracker_exposure()->set_tracker_id(trackerID);
    request->mutable_request_set_tracker_exposure()->set_value(static_cast<float>(value));

    ClientPSMoveAPI::register_callback(
        ClientPSMoveAPI::send_opaque_request(&request),
        AppStage_ColorCalibration::handle_tracker_set_exposure_response, this);
}

void AppStage_ColorCalibration::handle_tracker_set_exposure_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    ClientPSMoveAPI::eClientPSMoveResultCode ResultCode = response->result_code;
    ClientPSMoveAPI::t_response_handle response_handle = response->opaque_response_handle;
    AppStage_ColorCalibration *thisPtr = static_cast<AppStage_ColorCalibration *>(userdata);

    switch (ResultCode)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            const PSMoveProtocol::Response *response = GET_PSMOVEPROTOCOL_RESPONSE(response_handle);
            thisPtr->m_trackerExposure = response->result_set_tracker_exposure().new_exposure();
        } break;
    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            CLIENT_LOG_INFO("AppStage_ColorCalibration") << "Failed to set the tracker exposure!";
        } break;
    }
}

void AppStage_ColorCalibration::request_tracker_set_gain(int trackerID, double value)
{
    // Tell the psmove service that we want to change gain.
    RequestPtr request(new PSMoveProtocol::Request());
    request->set_type(PSMoveProtocol::Request_RequestType_SET_TRACKER_GAIN);
    request->mutable_request_set_tracker_gain()->set_tracker_id(trackerID);
    request->mutable_request_set_tracker_gain()->set_value(static_cast<float>(value));

    ClientPSMoveAPI::register_callback(
        ClientPSMoveAPI::send_opaque_request(&request),
        AppStage_ColorCalibration::handle_tracker_set_gain_response, this);
}

void AppStage_ColorCalibration::handle_tracker_set_gain_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    ClientPSMoveAPI::eClientPSMoveResultCode ResultCode = response->result_code;
    ClientPSMoveAPI::t_response_handle response_handle = response->opaque_response_handle;
    AppStage_ColorCalibration *thisPtr = static_cast<AppStage_ColorCalibration *>(userdata);

    switch (ResultCode)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            const PSMoveProtocol::Response *response = GET_PSMOVEPROTOCOL_RESPONSE(response_handle);
            thisPtr->m_trackerGain = response->result_set_tracker_gain().new_gain();
        } break;
    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            CLIENT_LOG_INFO("AppStage_ColorCalibration") << "Failed to set the tracker gain!";
        } break;
    }
}

void AppStage_ColorCalibration::request_tracker_set_option(
    int trackerID, TrackerOption &option, int new_option_index)
{
    // Tell the psmove service that we want to change gain.
    RequestPtr request(new PSMoveProtocol::Request());
    request->set_type(PSMoveProtocol::Request_RequestType_SET_TRACKER_OPTION);
    request->mutable_request_set_tracker_option()->set_tracker_id(trackerID);
    request->mutable_request_set_tracker_option()->set_option_name(option.option_name);
    request->mutable_request_set_tracker_option()->set_option_index(new_option_index);

    ClientPSMoveAPI::register_callback(
        ClientPSMoveAPI::send_opaque_request(&request),
        AppStage_ColorCalibration::handle_tracker_set_option_response, this);
}

void AppStage_ColorCalibration::handle_tracker_set_option_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    ClientPSMoveAPI::eClientPSMoveResultCode ResultCode = response->result_code;
    ClientPSMoveAPI::t_response_handle response_handle = response->opaque_response_handle;
    AppStage_ColorCalibration *thisPtr = static_cast<AppStage_ColorCalibration *>(userdata);

    switch (ResultCode)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            const PSMoveProtocol::Response *response = GET_PSMOVEPROTOCOL_RESPONSE(response_handle);
            int result_option_index = response->result_set_tracker_option().new_option_index();
            std::string option_name = response->result_set_tracker_option().option_name();

            // Find the option with the matching option_name
            auto it = std::find_if(
                thisPtr->m_trackerOptions.begin(),
                thisPtr->m_trackerOptions.end(),
                [&option_name](const TrackerOption &option) { 
                    return option.option_name == option_name; 
                });

            if (it != thisPtr->m_trackerOptions.end())
            {
                it->option_index = result_option_index;
            }
        } break;
    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            CLIENT_LOG_INFO("AppStage_ColorCalibration") << "Failed to set the tracker gain!";
        } break;
    }
}

void AppStage_ColorCalibration::request_tracker_get_settings(int trackerID)
{
    // Tell the psmove service that we want to change exposure.
    RequestPtr request(new PSMoveProtocol::Request());
    request->set_type(PSMoveProtocol::Request_RequestType_GET_TRACKER_SETTINGS);
    request->mutable_request_get_tracker_settings()->set_tracker_id(trackerID);

    ClientPSMoveAPI::register_callback(
        ClientPSMoveAPI::send_opaque_request(&request),
        AppStage_ColorCalibration::handle_tracker_get_settings_response, this);
}

void AppStage_ColorCalibration::handle_tracker_get_settings_response(
    const ClientPSMoveAPI::ResponseMessage *response,
    void *userdata)
{
    ClientPSMoveAPI::eClientPSMoveResultCode ResultCode = response->result_code;
    ClientPSMoveAPI::t_response_handle response_handle = response->opaque_response_handle;
    AppStage_ColorCalibration *thisPtr = static_cast<AppStage_ColorCalibration *>(userdata);

    switch (ResultCode)
    {
    case ClientPSMoveAPI::_clientPSMoveResultCode_ok:
        {
            const PSMoveProtocol::Response *response = GET_PSMOVEPROTOCOL_RESPONSE(response_handle);
            thisPtr->m_trackerExposure = response->result_tracker_settings().exposure();
            thisPtr->m_trackerGain = response->result_tracker_settings().gain();

            thisPtr->m_trackerOptions.clear();
            for (auto it = response->result_tracker_settings().option_sets().begin();
                it != response->result_tracker_settings().option_sets().end();
                ++it)
            {
                const PSMoveProtocol::OptionSet &srcOption = *it;
                AppStage_ColorCalibration::TrackerOption destOption;

                destOption.option_index = srcOption.option_index();
                destOption.option_name = srcOption.option_name();
                
                // Copy the option strings into the destOption
                std::for_each(
                    srcOption.option_strings().begin(), 
                    srcOption.option_strings().end(), 
                    [&destOption](const std::string &option_string) { 
                        destOption.option_strings.push_back(option_string); 
                    });

                thisPtr->m_trackerOptions.push_back(destOption);
            }
        } break;
    case ClientPSMoveAPI::_clientPSMoveResultCode_error:
    case ClientPSMoveAPI::_clientPSMoveResultCode_canceled:
        {
            CLIENT_LOG_INFO("AppStage_ColorCalibration") << "Failed to get the tracker settings!";
        } break;
    }
}