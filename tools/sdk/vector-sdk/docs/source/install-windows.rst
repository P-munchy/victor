.. _install-windows:

######################
Installation - Windows
######################

This guide provides instructions on installing the Vector SDK for computers running with a Windows operating system.

^^^^^^^^^^^^^
Prerequisites
^^^^^^^^^^^^^

* Vector is powered on.
* You have successfully created an Anki account.
* Vector has been set up with the Vector companion app.
* Vector is connected to the same network as your computer.
* You can see Vector's eyes on his screen.


^^^^^^^^^^^^^^^^^^^
Python Installation
^^^^^^^^^^^^^^^^^^^


Download the `Python 3.5.1 (or later) executable file from Python.org <https://www.python.org/downloads/>`_ and
run it on your computer.

.. important:: We recommend that you tick the "Add Python 3.5 to PATH" checkbox on the Setup screen.

^^^^^^^^^^^^^^^^
SDK Installation
^^^^^^^^^^^^^^^^

To install the SDK, type the following into the Command Prompt window::

    cd vector-sdk
    pip3 install --user vector-0.4-py3-none-any.whl

Note that the [camera] option adds support for processing images from Vector's camera.

^^^^^^^^^^^^^^^^^^^^^
Vector Authentication
^^^^^^^^^^^^^^^^^^^^^

To authenticate with the robot, type the following into the Terminal window::

    cd vector-sdk
    ./configure.py

You will be prompted for your robot's name, ip address and serial number. You will also be asked for your Anki login and password.

.. note:: Running `configure.py` will automatically download the Vector robot certificate to your computer and store credentials to allow you to connect to Vector. These credentials will be stored under your home directory in folder `.anki_vector`.

.. warning:: These credentials give full access to your robot, including camera stream, audio stream and data. Do not share these credentials.

^^^^^^^^^^^^^^^
Troubleshooting
^^^^^^^^^^^^^^^

Please see the :doc:`Troubleshooting </troubleshooting>` page for tips, or visit the `Anki SDK Forums <https://forums.anki.com/>`_ to ask questions, find solutions, or for general discussion.

----

`Terms and Conditions <https://www.anki.com/en-us/company/terms-and-conditions>`_ and `Privacy Policy <https://www.anki.com/en-us/company/privacy>`_

`Click here to return to the Anki Developer website. <http://developer.anki.com>`_
