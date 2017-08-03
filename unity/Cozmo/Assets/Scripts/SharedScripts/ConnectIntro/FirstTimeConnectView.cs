﻿using UnityEngine;
using UnityEngine.UI;
using Cozmo.UI;

public class FirstTimeConnectView : BaseView {

  public System.Action ConnectionFlowComplete;
  public System.Action ConnectionFlowQuit;

  [SerializeField]
  private CozmoButton _StartButton;

  [SerializeField]
  private ConnectionFlowController _ConnectionFlowPrefab;
  private ConnectionFlowController _ConnectionFlowInstance;

  [SerializeField]
  private SoundCheckModal _SoundCheckModalPrefab;
  private SoundCheckModal _SoundCheckModalInstance;

  [SerializeField]
  private PlaceCozmoOnChargerModal _PlaceCozmoOnChargerModalPrefab;
  private PlaceCozmoOnChargerModal _PlaceCozmoOnChargerModalInstance;

  [SerializeField]
  private ProfileCreationModal _ProfileCreationModalPrefab;
  private ProfileCreationModal _ProfileCreationModalInstance;

  [SerializeField]
  private CozmoButton _PrivacyPolicyButton;

  [SerializeField]
  private string _PrivacyPolicyFileName;

  [SerializeField]
  private CozmoButton _TermsOfUseButton;

  [SerializeField]
  private string _TermsOfUseFileName;

  [SerializeField]
  private CozmoButton _BuyCozmoButton;

  [SerializeField]
  private GameObject _DataCollectionPanel;

  [SerializeField]
  private CozmoButton _DataCollectionToggleButton;

  [SerializeField]
  private GameObject _DataCollectionIndicator;

  private void Awake() {

    DasTracker.Instance.TrackFirstTimeConnectStarted();
    RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.RobotDisconnected>(HandleRobotDisconnect);
    if (RobotEngineManager.Instance.RobotConnectionType == RobotEngineManager.ConnectionType.Mock) {
      _StartButton.Initialize(HandleMockButton, "start_button", "first_time_connect_dialog");
    }
    else {
      _StartButton.Initialize(HandleStartButton, "start_button", "first_time_connect_dialog");
    }

    InitializePrivacyPolicyButton();
    InitializeTermsOfUseButton();

    _BuyCozmoButton.Initialize(HandleBuyCozmoButton, "buy_cozmo_button", "first_time_connect_dialog");

    // hide data collection panel, unhide it later if locale wants it
    _DataCollectionPanel.gameObject.SetActive(false);

    // Request Locale gives us ability to get real platform dependent locale
    // whereas unity just gives us the language
    bool dataCollectionEnabled = DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.DataCollectionEnabled;
    if (DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.FirstTimeUserFlow) {
      DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.DataCollectionEnabled = true;
      RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.ResponseLocale>(HandleLocaleResponse);
      RobotEngineManager.Instance.Message.RequestLocale = Singleton<Anki.Cozmo.ExternalInterface.RequestLocale>.Instance;
      RobotEngineManager.Instance.SendMessage();
    }
    if (!dataCollectionEnabled) {
      // by default on so only needs to get set if false.
      SetDataCollection(dataCollectionEnabled);
    }
    // test code for fake german locale
#if UNITY_EDITOR
    if (RobotEngineManager.Instance.RobotConnectionType == RobotEngineManager.ConnectionType.Mock) {
      if (DataPersistence.DataPersistenceManager.Instance.Data.DebugPrefs.FakeGermanLocale) {
        Invoke("ShowDataCollectionPanel", 0.3f);
      }
    }
#endif
  }

  private void InitializePrivacyPolicyButton() {
    System.Action<BaseModal> privacyPolicyAlertCreated = (newModal) => {
      ScrollingTextModal scrollingTextModal = (ScrollingTextModal)newModal;
      scrollingTextModal.DASEventDialogName = "privacy_policy_view";
      scrollingTextModal.Initialize(Localization.Get(LocalizationKeys.kPrivacyPolicyTitle), Localization.ReadLocalizedTextFromFile(_PrivacyPolicyFileName));
    };

    UnityEngine.Events.UnityAction privacyPolicyPressedCallback = () => {
      UIManager.OpenModal(AlertModalLoader.Instance.ScrollingTextModalPrefab, new ModalPriorityData(), privacyPolicyAlertCreated);
    };

    _PrivacyPolicyButton.Initialize(privacyPolicyPressedCallback, "privacy_policy_button", "first_time_connect_dialog");
  }

  private void InitializeTermsOfUseButton() {
    System.Action<BaseModal> termsOfUseModalCreated = (newModal) => {
      ScrollingTextModal scrollingTextModal = (ScrollingTextModal)newModal;
      scrollingTextModal.DASEventDialogName = "terms_of_use_view";
      scrollingTextModal.Initialize(Localization.Get(LocalizationKeys.kLabelTermsOfUse), Localization.ReadLocalizedTextFromFile(_TermsOfUseFileName));
    };

    UnityEngine.Events.UnityAction termsOfUseButtonPressed = () => {
      UIManager.OpenModal(AlertModalLoader.Instance.ScrollingTextModalPrefab, new ModalPriorityData(), termsOfUseModalCreated);
    };

    _TermsOfUseButton.Initialize(termsOfUseButtonPressed, "terms_of_use_button", "first_time_connect_dialog");
  }

  private void OnDestroy() {
    if (_PlaceCozmoOnChargerModalInstance != null) {
      UIManager.CloseModalImmediately(_PlaceCozmoOnChargerModalInstance);
    }

    if (_ConnectionFlowInstance != null) {
      GameObject.Destroy(_ConnectionFlowInstance.gameObject);
    }
    RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.RobotDisconnected>(HandleRobotDisconnect);
    RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.ResponseLocale>(HandleLocaleResponse);
    DasTracker.Instance.TrackFirstTimeConnectEnded();
  }

  private void HandleStartButton() {
    if (DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.ProfileCreated) {
      if (DebugMenuManager.Instance.DemoMode) {
        StartConnectionFlow();
      }
      else {
        ShowPlaceCozmoOnCharger();
      }
    }
    else {
      UIManager.OpenModal(_SoundCheckModalPrefab, new ModalPriorityData(), HandleSoundCheckViewCreated);
    }
  }

  private void HandleSoundCheckViewCreated(BaseModal newSoundCheckModal) {
    _SoundCheckModalInstance = (SoundCheckModal)newSoundCheckModal;
    _SoundCheckModalInstance.OnSoundCheckComplete += HandleSoundCheckComplete;
  }

  private void HandleSoundCheckComplete() {
    _SoundCheckModalInstance.OnSoundCheckComplete -= HandleSoundCheckComplete;
    _SoundCheckModalInstance.DialogClosed += ShowProfileCreationScreen;
    UIManager.CloseModal(_SoundCheckModalInstance);
  }

  private void ShowProfileCreationScreen() {
    if (DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.DataCollectionEnabled) {
      UIManager.OpenModal(_ProfileCreationModalPrefab, new ModalPriorityData(), HandleProfileCreationViewCreated);
    }
    else {
      DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.ProfileCreated = true;
      DataPersistence.DataPersistenceManager.Instance.Save();
      ShowPlaceCozmoOnCharger();
    }
  }

  private void HandleProfileCreationViewCreated(BaseModal newProfileCreationModal) {
    _ProfileCreationModalInstance = (ProfileCreationModal)newProfileCreationModal;
    _ProfileCreationModalInstance.DialogClosed += HandleProfileCreationDone;
  }

  private void HandleProfileCreationDone() {
    _ProfileCreationModalInstance.DialogCloseAnimationFinished += ShowPlaceCozmoOnCharger;
    _ProfileCreationModalInstance.DialogClosed -= HandleProfileCreationDone;
    UIManager.CloseModal(_ProfileCreationModalInstance);
  }

  private void ShowPlaceCozmoOnCharger() {
    UIManager.OpenModal(_PlaceCozmoOnChargerModalPrefab, new ModalPriorityData(), HandlePlaceCozmoOnChargerViewCreated);
  }

  private void HandlePlaceCozmoOnChargerViewCreated(BaseModal newPlaceCozmoOnChargerModal) {
    _PlaceCozmoOnChargerModalInstance = (PlaceCozmoOnChargerModal)newPlaceCozmoOnChargerModal;
    _PlaceCozmoOnChargerModalInstance.OnConnectButton += HandleConnectButton;
  }

  private void HandleMockButton() {
    RobotEngineManager.Instance.MockConnect();
    if (DataPersistence.DataPersistenceManager.Instance.IsNewSessionNeeded) {
      DataPersistence.DataPersistenceManager.Instance.StartNewSession();
    }
    if (ConnectionFlowComplete != null) {
      ConnectionFlowComplete();
    }
  }

  private void HandleConnectButton() {
    _PlaceCozmoOnChargerModalInstance.DialogCloseAnimationFinished += StartConnectionFlow;
    _PlaceCozmoOnChargerModalInstance.OnConnectButton -= HandleConnectButton;
    UIManager.CloseModal(_PlaceCozmoOnChargerModalInstance);
  }

  private void StartConnectionFlow() {
    _ConnectionFlowInstance = GameObject.Instantiate(_ConnectionFlowPrefab.gameObject).GetComponent<ConnectionFlowController>();
    _ConnectionFlowInstance.ConnectionFlowComplete += HandleConnectionFlowComplete;
    _ConnectionFlowInstance.ConnectionFlowQuit += HandleConnectionFlowQuit;
    _ConnectionFlowInstance.StartConnectionFlow();
  }

  private void HandleConnectionFlowComplete() {
    if (_ConnectionFlowInstance != null) {
      GameObject.Destroy(_ConnectionFlowInstance.gameObject);
    }
    if (ConnectionFlowComplete != null) {
      ConnectionFlowComplete();
    }

  }

  public void HandleRobotDisconnect(Anki.Cozmo.ExternalInterface.RobotDisconnected message) {
    if (_ConnectionFlowInstance != null) {
      if (_ConnectionFlowInstance.ShouldIgnoreRobotDisconnect()) {
        return;
      }
      _ConnectionFlowInstance.HandleRobotDisconnect();
    }
    HandleConnectionFlowQuit();
  }

  private void HandleConnectionFlowQuit() {
    if (_ConnectionFlowInstance != null) {
      GameObject.Destroy(_ConnectionFlowInstance.gameObject);
    }
    if (ConnectionFlowQuit != null) {
      ConnectionFlowQuit();
    }
  }

  public void HandleLocaleResponse(Anki.Cozmo.ExternalInterface.ResponseLocale message) {
    string[] splitString = message.locale.Split(new char[] { '-', '_' });
    // Only german displays the option to opt out.
    if (splitString.Length >= 2) {
      if (splitString[1].ToLower().Equals("de")) {
        ShowDataCollectionPanel();
      }
    }
  }

  private void ShowDataCollectionPanel() {
    _DataCollectionPanel.gameObject.SetActive(true);
    _DataCollectionToggleButton.Initialize(HandleDataCollectionToggle, "data_collection_toggle", "first_time_connect_dialog");
  }

  private void HandleDataCollectionToggle() {
    SetDataCollection(!DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.DataCollectionEnabled);
  }

  private void SetDataCollection(bool val) {
    DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.DataCollectionEnabled = val;
    RobotEngineManager.Instance.Message.RequestDataCollectionOption =
                      Singleton<Anki.Cozmo.ExternalInterface.RequestDataCollectionOption>.Instance.Initialize(val);
    RobotEngineManager.Instance.SendMessage();
    _DataCollectionIndicator.gameObject.SetActive(val);
  }

  private void HandleBuyCozmoButton() {
    Application.OpenURL(Cozmo.Settings.DefaultSettingsValuesConfig.Instance.GetACozmoURL);
  }
}
