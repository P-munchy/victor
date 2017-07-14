(function(){
  'use strict';

  // register onload and body click events
  registerEvents();


  /**
   * Sets static strings to localized text
   * @returns {void}
   */
  function setLocalizedText() {
    setText('#app-title', $t('codeLab.projects.modalTitle'));

    setText('#attribution-label', '');
    setText('#tutorial-label', $t('codeLab.projects.tutorialLabel'));
    setText('#new-project-label', $t('codeLab.projects.newProjectButtonLabel'));


    setText('#prototype-sample-project .project-type', $t('codeLab.projects.projectType.sampleProject'));
    setText('#prototype-user-project .project-type', $t('codeLab.projects.projectType.personalProject'));
  }


  function registerEvents(){

    window.addEventListener('DOMContentLoaded', function() {
      // set localization strings after document body has been parsed
      setLocalizedText();

      // start rendering projects
      CozmoAPI.getProjects('window.renderProjects');

      // FOR DEV ONLY - DO NOT TURN ON IN COMMIT
      // _devLoadProjects();
    });

    // register main click handler for the document
    document.body.addEventListener('click', _handleBodyClick);

    // activate CSS for taps by registering a touchstart event
    document.addEventListener("touchstart", function(){}, true);

    // prevent page from elastic scrolling to reveal whitespace, but
    //  allow the project list to scroll
    document.addEventListener('touchmove', function(event){
      event.preventDefault();
    });
    document.querySelector('#projects-list').addEventListener('touchmove', function(event){
      // allow the project list to scroll by touch
      event.stopPropagation();
    });
  }

  /**
   * Main click event handler for document body.
   * Relies on attribute "data-type" values on clickable elements to
   *  respond to the click.
   * @param {DOMEvent} event - click event on the document
   * @returns {void}
   */
  function _handleBodyClick(event) {
    var typeElem = _getDataTypeElement(event.target);
    if (!typeElem) return;
    var type = typeElem.getAttribute('data-type');

    var playClickSound = true;

    switch(type) {
      case 'btn-create-new-project':
        // we play the click sound before calling unity to make sure sound plays on android
        playClickSound = false;
        handleCreateNewProjectClick();
        break;
      case 'load-sample-project':
        // we play the click sound before calling unity to make sure sound plays on android
        playClickSound = false;
        handleSampleProjectClick(typeElem.dataset.uuid);
        break;
      case 'load-user-project':
        // we play click the sound before calling unity to make sure sound plays on android
        playClickSound = false;
        handleUserProjectClick(typeElem.dataset.uuid);
        break;
      case 'btn-confirm-delete-project':
        showConfirmDeleteProjectModal(typeElem.parentNode);
        break;
      case 'btn-close-page':
        handleClosePage();
        break;
      case 'tutorial-link':
        playClickSound = false;
        handleTutorialLinkClick(typeElem);
        break;

      default:
        playClickSound = false;
    }

    if (playClickSound && window.player) {
      window.player.play('click');
    }
  }


  function handleClosePage() {
    // open a dialog confirming that they want to exit the Code Lab
    ModalConfirm.open({
      title: $t('codeLab.projects.confirmQuit.promptText'),
      prompt: '',
      confirmButtonLabel: $t('codeLab.projects.confirmQuit.confirmButton.labelText'),
      cancelButtonLabel: $t('codeLab.projects.confirmQuit.cancelButton.labelText'),
      confirmCallback: function(result) {
        if (result) {
          CozmoAPI.closeCodeLab();
        }
      }
    });
  }

  /**
   * Handles notifying Unity that the user wants to create a new project
   * @returns {void}
   */
  function handleCreateNewProjectClick() {
    // Play the click sound before calling unity to insure it is played on Android
    if (window.player) {
      window.player.play('click');
    }
    CozmoAPI.createNewProject();
  }

  /**
   * Handles notifying Unity that the user wants to load a sample project
   * @returns {void}
   */
  function handleSampleProjectClick(uuid) {
    // Play the click sound before calling unity to insure it is played on Android
    if (window.player) {
      window.player.play('click');
    }
    CozmoAPI.openSampleProject(uuid);
  }

  /**
   * Handles notifying Unity that the user wants to load a user's personal project
   * @returns {void}
   */
  function handleUserProjectClick(uuid) {
    // Play the click sound before calling unity to insure it is played on Android
    if (window.player) {
      window.player.play('click');
    }
    CozmoAPI.openUserProject(uuid);
  }

  /**
   * Plays sound before opening the tutorial
   * @param {HTMLElement} elem - tutorial link button element (assuming <a> (anchor) tag)
   * @returns {void}
   */
  function handleTutorialLinkClick(elem) {
    // if sound player is present, play a click sound before closing
    if (window.player) {
      window.player.play('click');
    }

    // to ensure the sound has time to play before navigating, intercept the
    //  anchor navigation and wait until after the sound plays to change pages

    // prevent the anchor tag from navigating to the next page until there is enough time for sound to play
    event.preventDefault();

    // save the URL of the anchor tag and navigate there after sound should finish playing
    var url = elem.getAttribute('href');
    setTimeout(function(){
      // navigate to anchor tag's url after sound finishes
      window.location.href = url + '?locale=' + LOCALE;
    }, 50);
  }

  /**
   * returns closest element with a "data-type" attribute
   * @param {HTMLElement} elem - elem to search for data-type attribute on self or ancestors
   * @returns {HTMLElement|null} closest element with data-type attribute, or null
   */
  function _getDataTypeElement(elem){
    while (elem && elem !== document) {
      var type = elem.getAttribute('data-type');
      if (type) {
        return elem;  // return element with data-type attribute
      } else {
        elem = elem.parentNode;  // search next ancestor
      }
    }
    return null;  // no element with data-type attribute found
  }


  // **********************
  // Project Card Rendering
  // **********************

  /**
   * Renders cards for user and sample projects
   * @param {Array} userProjects - array of user projects to render
   * @param {Array} sampleProjects - array of sample projects to render
   */
  window.renderProjects = function(userProjectsStr, sampleProjectsStr) {
    var projectList = document.querySelector('#projects-list');
    var i, card;

    // render user projects and add them to the UI
    var userProjects = JSON.parse(userProjectsStr);
    var sampleProjects = JSON.parse(sampleProjectsStr);

    if (Array.isArray(userProjects)) {
      for(i = 0; i < userProjects.length; i++) {
        card = makeUserProjectCard(userProjects[i], i);
        projectList.appendChild(card);
      }
    }

    // render sample projects and add them to the UI
    if (Array.isArray(sampleProjects)) {
      for(i = 0; i < sampleProjects.length; i++) {
        card = makeSampleProjectCard(sampleProjects[i]);
        projectList.appendChild(card);
      }
    }
  };


  /**
   * Creates DOM elements for a card representing a sample project
   * @param {Object} projectData - fields about the project
   * @returns {HTMLElement} returns unattached DOM element for sample project card
   */
  function makeSampleProjectCard(projectData) {
    // clone the sample project card prototype
    var project = document.querySelector('#prototype-sample-project').cloneNode(true);
    project.removeAttribute('id');

    // add the project data to the element
    setProjectData(project, projectData);

    // set the project title to the localized name
    project.querySelector('.project-title').textContent = $t(projectData.ProjectName);

    // set the icon to match the content of the sample project
    var icon = projectData.ProjectIconName;
    var iconPath = '../lib/blocks/media/icons/' + icon + '.svg';
    project.querySelector('.block-icon').setAttribute('src', iconPath);

    var projectsUI = document.querySelector('#projects');

    // add a style to color the puzzle piece SVG after it loads
    var block = project.querySelector('.block');
    var type = getBlockIconColor(icon);
    block.setAttribute('src', 'images/icon_block_' + type + '.svg');
    block.addEventListener('load', function(elem) {
      project.style.visibility = 'visible';

      // show the entire Projects UI when the first sample project is ready to be shown
      projectsUI.style.visibility = 'visible';
    });
    return project;
  }


  /**
   * Creates DOM elements for a card representing a user project
   * @param {Object} projectData - fields about the project
   * @param {Number} order - the position in personal cards this card appears (used for coloring)
   * @returns {HTMLElement} returns unattached DOM element for peronal project card
   */
  function makeUserProjectCard(projectData, order) {
    // clone the user project prototype
    var project = document.querySelector('#prototype-user-project').cloneNode(true);
    project.removeAttribute('id');

    // add the project data to the element
    setProjectData(project, projectData);

    // set the project title
    project.querySelector('.project-title').textContent = projectData.ProjectName;

    // set color of puzzle pieces on background card based on order position
    var cardUrl = 'images/framing_widgetBackground_player' + ((order % 3) + 1) + '.svg';
    var card = project.querySelector('.project-card');
    card.setAttribute('src', cardUrl);
    card.addEventListener('load', function(){
      // show the card once the background has loaded
      project.style.display = 'inline-block';
    });

    return project;
  }

  /**
   * Copies relevent project data fields to data attributes of project DOM element
   * @param {HTMLElement} projectElem - project container element to copy fields to
   * @param {Object} projectData - project fields to copy to project's dataset
   * @returns {void}
   */
  function setProjectData(projectElem, projectData) {
    projectElem.setAttribute('data-uuid', projectData.ProjectUUID);
    projectElem.setAttribute('data-project-name', projectData.ProjectName);
  }




  // *********************************
  // Confirm Delete User Project Modal
  // *********************************

  /**
   * Displays modal asking user to confirm deleting a user project
   * @param {HTMLElement} projectElem - element containing the project
   * @returns {void}
   */
  function showConfirmDeleteProjectModal(projectElem){
    var uuid = projectElem.getAttribute('data-uuid');
    var projectName = projectElem.getAttribute('data-project-name');


    ModalConfirm.open({
      title: $t('codeLab.projects.confirmDeleteProject.confirmPromptTitle', projectName),
      confirmButtonLabel: $t('codeLab.projects.confirmDeleteProject.confirmButton.labelText'),
      cancelButtonLabel: $t('codeLab.projects.confirmDeleteProject.cancelButton.labelText'),
      prompt: $t('codeLab.projects.confirmDeleteProject.confirmPrompt'),
      confirmCallback: function(result) {
        if (result) {
          if (window.player) {
            window.player.play('delete');
          }
          CozmoAPI.deleteProject(uuid);
        } else {
          window.player.play('click');
        }
      }
    });
  }




  // ***************
  // Cozmo/Unity API
  // ***************

  /**
   * CozmoAPI is a wrapper over our Unity API so that I can fake
   *  it during development.
   */
  var CozmoAPI = function(){

    function getProjects(callbackName) {
      window.getCozmoUserAndSampleProjectLists(callbackName);
    }

    function createNewProject() {
      window.requestToCreateCozmoProject();
    }

    function openUserProject(uuid) {
      window.requestToOpenCozmoUserProject(uuid);
    }

    function openSampleProject(uuid) {
      window.requestToOpenCozmoSampleProject(uuid);
    }

    function deleteProject(uuid) {
      // remove the project from the display
      var projectElem = document.querySelector('.project[data-uuid="'+uuid+'"]');
      projectElem.parentNode.removeChild(projectElem);

      // notify Unity to actually delete the project
      window.deleteCozmoUserProject(uuid);
    }

    function closeCodeLab() {
      window.closeCodeLab();
    }

    return {
      getProjects: getProjects,
      createNewProject: createNewProject,
      openUserProject: openUserProject,
      openSampleProject: openSampleProject,
      deleteProject: deleteProject,
      closeCodeLab: closeCodeLab
    };
  }();

  // *****************
  // Utility functions
  // *****************

  /**
   * DEVELOPMENT ONLY
   * Used to render projects into page when run from outside the Unity application.
   * @returns {void}
   */
  function _devLoadProjects() {

    getJSON('../sample-projects.json', function(sampleProjects) {
      // copy 3 of the sample projects and render them as user projects
      var userProjects = sampleProjects.slice(0,3);

      // deep copy the fake user projects so that translations of sample projects are not effected
      userProjects = JSON.parse(JSON.stringify(userProjects));

      // translate the sample project names
      for (var i=0; i < userProjects.length; i++) {
        userProjects[i].ProjectName = $t(userProjects[i].ProjectName);
      }

      window.renderProjects(JSON.stringify(userProjects), JSON.stringify(sampleProjects));
    });
  }

})();

