#include <flutter_email_sender_method_channel/flutter_email_sender_method_channel_plugin_c_api.h>

#include <flutter/plugin_registrar_windows.h>

#include "flutter_email_sender_method_channel_plugin.h"

void FlutterEmailSenderMethodChannelPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  FlutterEmailSenderMethodChannelPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
