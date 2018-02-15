#!/usr/bin/env bash

# Handy Victor Aliases from Al Chaussee
# =====================================
# They can be called from anywhere within the git project to build, deploy, and run on physical robots.
# Call source on this file from your .bash_profile if you always want the latest version of these aliases 
# or copy its contents into your .bash_profile.

alias GET_GIT_ROOT='export GIT_PROJ_ROOT=`git rev-parse --show-toplevel`'

alias victor_restart='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_restart.sh'
alias victor_start='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_start.sh'
alias victor_stop='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_stop.sh'

alias victor_build='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_build.sh'
alias victor_deploy='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_deploy.sh'
alias victor_deploy_run='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_deploy_run.sh'
alias victor_build_run='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_build_run.sh'
alias victor_assets='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_assets.sh'
alias victor_assets_force='GET_GIT_ROOT; ${GIT_PROJ_ROOT}/project/victor/scripts/victor_assets_force.sh'

alias victor_log='adb logcat mm-camera:S mm-camera-intf:S mm-camera-eztune:S mm-camera-sensor:S mm-camera-img:S cnss-daemon:S cozmoengined:S ServiceManager:S chatty:S'
alias victor_ble='GET_GIT_ROOT; node ${GIT_PROJ_ROOT}/tools/victor-ble-cli/index.js'

# If you have lnav...
alias victor_lnav='adb logcat | lnav -c '\'':filter-out mm-camera'\'' -c '\'':filter-out cnss-daemon'\'' -c '\'':filter-out ServiceManager'\'' -c '\'':filter-out chatty'\'''
