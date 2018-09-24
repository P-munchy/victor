/*
 * File:          cozmoAnim/animEngine.cpp
 * Date:          6/26/2017
 *
 * Description:   A platform-independent container for spinning up all the pieces
 *                required to run Cozmo Animation Process.
 *
 * Author: Kevin Yoon
 *
 * Modifications:
 */

#include "cozmoAnim/alexa.h"
#include "cozmoAnim/alexaClient.h"
#include "cozmoAnim/alexaLogger.h"
#include "cozmoAnim/alexaMicrophone.h"
#include "cozmoAnim/alexaSpeaker.h"
//#include "cozmoAnim/alexaSpeechSynthesizer.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"

#include <ACL/Transport/HTTP2TransportFactory.h>
#include <AVSCommon/AVS/Initialization/AlexaClientSDKInit.h>
#include <AVSCommon/Utils/Configuration/ConfigurationNode.h>
#include <AVSCommon/Utils/DeviceInfo.h>
#include <AVSCommon/Utils/LibcurlUtils/HTTPContentFetcherFactory.h>
#include <AVSCommon/Utils/LibcurlUtils/HttpPut.h>
#include <AVSCommon/Utils/Logger/Logger.h>
#include <AVSCommon/Utils/Logger/LoggerSinkManager.h>
#include <AVSCommon/Utils/Network/InternetConnectionMonitor.h>
#include <Alerts/Storage/SQLiteAlertStorage.h>
#include <Audio/AudioFactory.h>
#include <Bluetooth/SQLiteBluetoothStorage.h>
#include <CBLAuthDelegate/CBLAuthDelegate.h>
#include <CBLAuthDelegate/SQLiteCBLAuthDelegateStorage.h>
#include <CapabilitiesDelegate/CapabilitiesDelegate.h>
#include <ContextManager/ContextManager.h>
#include <Notifications/SQLiteNotificationsStorage.h>
#include <SQLiteStorage/SQLiteMiscStorage.h>
#include <Settings/SQLiteSettingStorage.h>
//#include <DefaultClient/DefaultClient.h>

#include <memory>
#include <vector>
#include <iostream>

namespace Anki {
namespace Vector {
  
namespace {
    
  /// The sample rate of microphone audio data.
  static const unsigned int SAMPLE_RATE_HZ = 16000;
  
  /// The number of audio channels.
  static const unsigned int NUM_CHANNELS = 1;
  
  /// The size of each word within the stream.
  static const size_t WORD_SIZE = 2;
  
  /// The maximum number of readers of the stream.
  static const size_t MAX_READERS = 10;
  
  /// The amount of audio data to keep in the ring buffer.
  static const std::chrono::seconds AMOUNT_OF_AUDIO_DATA_IN_BUFFER = std::chrono::seconds(15);
  
  /// The size of the ring buffer.
  static const size_t BUFFER_SIZE_IN_SAMPLES = (SAMPLE_RATE_HZ)*AMOUNT_OF_AUDIO_DATA_IN_BUFFER.count();
  
  /// Key for the root node value containing configuration values for SampleApp.
  static const std::string SAMPLE_APP_CONFIG_KEY("sampleApp");
  
  /// Key for the @c firmwareVersion value under the @c SAMPLE_APP_CONFIG_KEY configuration node.
  static const std::string FIRMWARE_VERSION_KEY("firmwareVersion");
  
  /// Key for the @c endpoint value under the @c SAMPLE_APP_CONFIG_KEY configuration node.
  static const std::string ENDPOINT_KEY("endpoint");
    
  
  const std::string kConfig = R"4Nk1({
    "cblAuthDelegate":{
      // Path to CBLAuthDelegate's database file. e.g. /home/ubuntu/Build/cblAuthDelegate.db
      // Note: The directory specified must be valid.
      // The database file (cblAuthDelegate.db) will be created by SampleApp, do not create it yourself.
      // The database file should only be used for CBLAuthDelegate (don't use it for other components of SDK)
      "databaseFilePath":"/data/data/com.anki.victor/persistent/alexa/cblAuthDelegate.db"
    },
    "deviceInfo":{
      // Unique device serial number. e.g. 123456
      "deviceSerialNumber":"123457",
      // The Client ID of the Product from developer.amazon.com
      "clientId": "amzn1.application-oa2-client.35a58ee8f3444563aed328cb189da216",
      // Product ID from developer.amazon.com
      "productId": "test_product_1"
    },
    "capabilitiesDelegate":{
      // The endpoint to connect in order to send device capabilities.
      // This will only be used in DEBUG builds.
      // e.g. "endpoint": "https://api.amazonalexa.com"
      // Override the message to be sent out to the Capabilities API.
      // This will only be used in DEBUG builds.
      // e.g. "overridenCapabilitiesPublishMessageBody": {
      //          "envelopeVersion":"20160207",
      //          "capabilities":[
      //              {
      //                "type":"AlexaInterface",
      //                "interface":"Alerts",
      //                "version":"1.1"
      //              }
      //          ]
      //      }
    },
    "miscDatabase":{
      // Path to misc database file. e.g. /home/ubuntu/Build/miscDatabase.db
      // Note: The directory specified must be valid.
      // The database file (miscDatabase.db) will be created by SampleApp, do not create it yourself.
      "databaseFilePath":"/data/data/com.anki.victor/persistent/alexa/miscDatabase.db"
    },
    "alertsCapabilityAgent":{
      // Path to Alerts database file. e.g. /home/ubuntu/Build/alerts.db
      // Note: The directory specified must be valid.
      // The database file (alerts.db) will be created by SampleApp, do not create it yourself.
      // The database file should only be used for alerts (don't use it for other components of SDK)
      "databaseFilePath":"/data/data/com.anki.victor/persistent/alexa/alerts.db"
    },
    "settings":{
      // Path to Settings database file. e.g. /home/ubuntu/Build/settings.db
      // Note: The directory specified must be valid.
      // The database file (settings.db) will be created by SampleApp, do not create it yourself.
      // The database file should only be used for settings (don't use it for other components of SDK)
      "databaseFilePath":"/data/data/com.anki.victor/persistent/alexa/settings.db",
      "defaultAVSClientSettings":{
        // Default language for Alexa.
        // See https://developer.amazon.com/docs/alexa-voice-service/settings.html#settingsupdated for valid values.
        "locale":"en-US"
      }
    },
    "bluetooth" : {
      // Path to Bluetooth database file. e.g. /home/ubuntu/Build/bluetooth.db
      // Note: The directory specified must be valid.
      // The database file (bluetooth.db) will be created by SampleApp, do not create it yourself.
      // The database file should only be used for bluetooth (don't use it for other components of SDK)
      "databaseFilePath":"/data/data/com.anki.victor/persistent/alexa/bluetooth.db"
    },
    "certifiedSender":{
      // Path to Certified Sender database file. e.g. /home/ubuntu/Build/certifiedsender.db
      // Note: The directory specified must be valid.
      // The database file (certifiedsender.db) will be created by SampleApp, do not create it yourself.
      // The database file should only be used for certifiedSender (don't use it for other components of SDK)
      "databaseFilePath":"/data/data/com.anki.victor/persistent/alexa/certifiedsender.db"
    },
    "notifications":{
      // Path to Notifications database file. e.g. /home/ubuntu/Build/notifications.db
      // Note: The directory specified must be valid.
      // The database file (notifications.db) will be created by SampleApp, do not create it yourself.
      // The database file should only be used for notifications (don't use it for other components of SDK)
      "databaseFilePath":"/data/data/com.anki.victor/persistent/alexa/notifications.db"
    },
    "sampleApp":{
      // To specify if the SampleApp supports display cards.
      "displayCardsSupported":true
      // The firmware version of the device to send in SoftwareInfo event.
      // Note: The firmware version should be a positive 32-bit integer in the range [1-2147483647].
      // e.g. "firmwareVersion": 123
      // The default endpoint to connect to.
      // See https://developer.amazon.com/docs/alexa-voice-service/api-overview.html#endpoints for regions and values
      // e.g. "endpoint": "https://avs-alexa-na.amazon.com"
      
      // Example of specifying suggested latency in seconds when openning PortAudio stream. By default,
      // when this paramater isn't specified, SampleApp calls Pa_OpenDefaultStream to use the default value.
      // See http://portaudio.com/docs/v19-doxydocs/structPaStreamParameters.html for further explanation
      // on this parameter.
      //"portAudio":{
      //    "suggestedLatency": 0.150
      //}
    },
    
    // Example of specifying output format and the audioSink for the gstreamer-based MediaPlayer bundled with the SDK.
    // Many platforms will automatically set the output format correctly, but in some cases where the hardware requires
    // a specific format and the software stack is not automatically setting it correctly, these parameters can be used
    // to manually specify the output format.  Supported rate/format/channels values are documented in detail here:
    // https://gstreamer.freedesktop.org/documentation/design/mediatype-audio-raw.html
    //
    // By default the "autoaudiosink" element is used in the pipeline.  This element automatically selects the best sink
    // to use based on the configuration in the system.  But sometimes the wrong sink is selected and that prevented sound
    // from being played.  A new configuration is added where the audio sink can be specified for their system.
    // "gstreamerMediaPlayer":{
    //     "outputConversion":{
    //         "rate":16000,
    //         "format":"S16LE",
    //         "channels":1
    //     },
    //     "audioSink":"autoaudiosink"
    // },
    
    // Example of specifiying curl options that is different from the default values used by libcurl.
    // "libcurlUtils":{
    //
    //     By default libcurl is built with paths to a CA bundle and a directory containing CA certificates. You can
    //     direct the AVS Device SDK to configure libcurl to use an additional path to directories containing CA
    //     certificates via the CURLOPT_CAPATH setting.  Additional details of this curl option can be found in:
    //     https://curl.haxx.se/libcurl/c/CURLOPT_CAPATH.html
    //     "CURLOPT_CAPATH":"INSERT_YOUR_CA_CERTIFICATE_PATH_HERE",
    //
    //     You can specify the AVS Device SDK to use a specific outgoing network interface.  More information of
    //     this curl option can be found here:
    //     https://curl.haxx.se/libcurl/c/CURLOPT_INTERFACE.html
    //     "CURLOPT_INTERFACE":"INSERT_YOUR_INTERFACE_HERE"
    // },
    
    // Example of specifying a default log level for all ModuleLoggers.  If not specified, ModuleLoggers get
    // their log level from the sink logger.
     "logging":{
         "logLevel":"DEBUG9"
     }
  
    // Example of overriding a specific ModuleLogger's log level whether it was specified by the default value
    // provided by the logging.logLevel value (as in the above example) or the log level of the sink logger.
    // "acl":{
    //     "logLevel":"DEBUG9"
    // }
  }
  
  
  )4Nk1";

}
  
  using namespace alexaClientSDK;
  
void Alexa::Init()
{
  if( Util::FileUtils::DirectoryDoesNotExist( "/data/data/com.anki.victor/persistent/alexa" ) ) {
    Util::FileUtils::CreateDirectory( "/data/data/com.anki.victor/persistent/alexa", false, true );
  }
  
  
  std::vector<std::shared_ptr<std::istream>> configJsonStreams;
  
  // todo: load from data loader
  configJsonStreams.push_back( std::shared_ptr<std::istringstream>(new std::istringstream(kConfig)) );
  
  // for (auto configFile : configFiles) {
  //   if (configFile.empty()) {
  //     alexaClientSDK::sampleApp::ConsolePrinter::simplePrint("Config filename is empty!");
  //     return;
  //   }
  
  //   auto configInFile = std::shared_ptr<std::ifstream>(new std::ifstream(configFile));
  //   if (!configInFile->good()) {
  //     ACSDK_CRITICAL(LX("Failed to read config file").d("filename", configFile));
  //     alexaClientSDK::sampleApp::ConsolePrinter::simplePrint("Failed to read config file " + configFile);
  //     return;
  //   }
  
  //   configJsonStreams.push_back(configInFile);
  // }
  PRINT_NAMED_WARNING("WHATNOW", "calling");
  
  #define LX(event) avsCommon::utils::logger::LogEntry(__FILE__, event)
  
  // todo: this should be called befpre any other threads start (acccording to docs)
  if (!avsCommon::avs::initialization::AlexaClientSDKInit::initialize(configJsonStreams)) {
    //ACSDK_CRITICAL(LX("Failed to initialize SDK!"));
    PRINT_NAMED_WARNING("WHATNOW", "failed to init");
    return;
  }
  
  const auto& config = avsCommon::utils::configuration::ConfigurationNode::getRoot();
  
  /*
   * Creating customerDataManager which will be used by the registrationManager and all classes that extend
   * CustomerDataHandler
   */
  auto customerDataManager = std::make_shared<registrationManager::CustomerDataManager>();
  
  /*
   * Creating the deviceInfo object
   */
  std::shared_ptr<avsCommon::utils::DeviceInfo> deviceInfo = avsCommon::utils::DeviceInfo::create(config);
  if (!deviceInfo) {
    ACSDK_CRITICAL(LX("Creation of DeviceInfo failed!"));
    PRINT_NAMED_WARNING("WHATNOW", "deviceinfo failed");
    return;
  }
  
  /*
   * Creating the UI component that observes various components and prints to the console accordingly.
   */
  auto userInterfaceManager = std::make_shared<AlexaLogger>();
  
  //  /*
  //   * Creating the AuthDelegate - this component takes care of LWA and authorization of the client.
  //   */
  auto authDelegateStorage = authorization::cblAuthDelegate::SQLiteCBLAuthDelegateStorage::create(config);
  std::shared_ptr<avsCommon::sdkInterfaces::AuthDelegateInterface> authDelegate =
  authorization::cblAuthDelegate::CBLAuthDelegate::create(
                                                          config, customerDataManager, std::move(authDelegateStorage), userInterfaceManager, nullptr, deviceInfo);
  
  // Creating the misc DB object to be used by various components.
  std::shared_ptr<alexaClientSDK::storage::sqliteStorage::SQLiteMiscStorage> miscStorage =
    alexaClientSDK::storage::sqliteStorage::SQLiteMiscStorage::create(config);
  
  // Create HTTP Put handler
  std::shared_ptr<avsCommon::utils::libcurlUtils::HttpPut> httpPut =
    avsCommon::utils::libcurlUtils::HttpPut::create();

  if (!authDelegate) {
    ACSDK_CRITICAL(LX("Creation of AuthDelegate failed!"));
    PRINT_NAMED_WARNING("WHATNOW", "Creation of AuthDelegate failed!");
    return;
  }
  authDelegate->addAuthObserver(userInterfaceManager);
  
  /*
   * Creating the CapabilitiesDelegate - This component provides the client with the ability to send messages to the
   * Capabilities API.
   */
  m_capabilitiesDelegate = alexaClientSDK::capabilitiesDelegate::CapabilitiesDelegate::create(
    authDelegate, miscStorage, httpPut, customerDataManager, config, deviceInfo
  );
  if (!m_capabilitiesDelegate) {
    PRINT_NAMED_WARNING("WHATNOW", "Creation of CapabilitiesDelegate failed!");
    return;
  }
  m_capabilitiesDelegate->addCapabilitiesObserver(userInterfaceManager);
  
  auto messageStorage = alexaClientSDK::certifiedSender::SQLiteMessageStorage::create(config);
  
  // setup "speaker"
  m_TTSSpeaker = std::make_shared<AlexaSpeaker>();
  
  m_client = AlexaClient::create(
    deviceInfo,
    customerDataManager,
    authDelegate,
    std::move(messageStorage),
    {userInterfaceManager},
    {userInterfaceManager},
    m_capabilitiesDelegate,
    m_TTSSpeaker
  );
  
  if (!m_client) {
    ACSDK_CRITICAL(LX("Failed to create default SDK client!"));
    return;
  }
  
  /*
   * Creating the buffer (Shared Data Stream) that will hold user audio data. This is the main input into the SDK.
   */
  size_t bufferSize = alexaClientSDK::avsCommon::avs::AudioInputStream::calculateBufferSize(
                                                                                            BUFFER_SIZE_IN_SAMPLES, WORD_SIZE, MAX_READERS);
  auto buffer = std::make_shared<alexaClientSDK::avsCommon::avs::AudioInputStream::Buffer>(bufferSize);
  std::shared_ptr<alexaClientSDK::avsCommon::avs::AudioInputStream> sharedDataStream =
  alexaClientSDK::avsCommon::avs::AudioInputStream::create(buffer, WORD_SIZE, MAX_READERS);
  
  if (!sharedDataStream) {
    ACSDK_CRITICAL(LX("Failed to create shared data stream!"));
    return;
  }
  
  alexaClientSDK::avsCommon::utils::AudioFormat compatibleAudioFormat;
  compatibleAudioFormat.sampleRateHz = SAMPLE_RATE_HZ;
  compatibleAudioFormat.sampleSizeInBits = WORD_SIZE * CHAR_BIT;
  compatibleAudioFormat.numChannels = NUM_CHANNELS;
  compatibleAudioFormat.endianness = alexaClientSDK::avsCommon::utils::AudioFormat::Endianness::LITTLE;
  compatibleAudioFormat.encoding = alexaClientSDK::avsCommon::utils::AudioFormat::Encoding::LPCM;
  
  /*
   * Creating each of the audio providers. An audio provider is a simple package of data consisting of the stream
   * of audio data, as well as metadata about the stream. For each of the three audio providers created here, the same
   * stream is used since this sample application will only have one microphone.
   */
  
  // Creating tap to talk audio provider
  bool tapAlwaysReadable = true;
  bool tapCanOverride = true;
  bool tapCanBeOverridden = true;
  
  m_tapToTalkAudioProvider = std::make_shared<alexaClientSDK::capabilityAgents::aip::AudioProvider>(
                                                                                                    sharedDataStream,
                                                                              compatibleAudioFormat,
                                                                              alexaClientSDK::capabilityAgents::aip::ASRProfile::NEAR_FIELD,
                                                                              tapAlwaysReadable,
                                                                              tapCanOverride,
                                                                              tapCanBeOverridden);
  

  
//  m_interactionManager = std::make_shared<alexaClientSDK::sampleApp::InteractionManager>(
//                                                                                         client, micWrapper, userInterfaceManager, holdToTalkAudioProvider, tapToTalkAudioProvider, m_guiRenderer);
//
//  m_dialogUXStateAggregator->addObserver(m_interactionManager);
  
  //m_client->addAlexaDialogStateObserver(m_interactionManager);
  
//  // Creating the input observer.
//  m_userInputManager = alexaClientSDK::sampleApp::UserInputManager::create(m_interactionManager, consoleReader);
//  if (!m_userInputManager) {
//    ACSDK_CRITICAL(LX("Failed to create UserInputManager!"));
//    return;
//  }
  
  // create a microphone
  m_microphone = AlexaMicrophone::create(sharedDataStream);
  m_microphone->startStreamingMicrophoneData();
  
  m_capabilitiesDelegate->addCapabilitiesObserver(m_client);
  
  // try connecting
  m_client->Connect( m_capabilitiesDelegate );
  
  PRINT_NAMED_WARNING("WHATNOW", "worked");
  return;

}
  
  
void Alexa::ButtonPress()
{
  if( !m_client->notifyOfTapToTalk(*m_tapToTalkAudioProvider).get() ) {
    PRINT_NAMED_WARNING("WHATNOW", "Failed to notify tap to talk");
  }
}

void Alexa::ProcessMicDataPayload(const RobotInterface::MicData& payload)
{
  if( m_microphone ) {
    m_microphone->ProcessMicDataPayload(payload);
  }
}
  
  
} // namespace Vector
} // namespace Anki
