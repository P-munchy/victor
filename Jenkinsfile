#!/usr/bin/env groovy

@NonCPS
def getListOfOnlineNodesForLabel(label) {
  def nodes = []
  jenkins.model.Jenkins.instance.computers.each { c ->
    if (c.node.labelString.contains(label) && c.node.toComputer().isOnline()) {
      nodes.add(c.node.selfLabel.name)
    }
  }
  return nodes
}

node('victor-slaves') {
    withEnv(['CONFIGURATION=release']) {
        def victorBuildImage = docker.image('victor-build-img:99')
        victorBuildImage.inside {
            checkout([
                $class: 'GitSCM',
                branches: scm.branches,
                doGenerateSubmoduleConfigurations: scm.doGenerateSubmoduleConfigurations,
                extensions: scm.extensions,
                userRemoteConfigs: scm.userRemoteConfigs
            ])
            stage('Update submodules') {
                sh 'git submodule update --init --recursive'
                sh 'git submodule update --recursive'
            }
            stage('json lint') {
                sh './lib/util/tools/build/jsonLint/jsonLint.sh resources'
            }
            withEnv(["HOME=${env.WORKSPACE}"]) {
                stage('Build') {
                    sh './project/victor/build-victor.sh -c Release'
                }
            }
        }
        def macBuildAgentsList = getListOfOnlineNodesForLabel('mac-slaves')
        if (!macBuildAgentsList.isEmpty()) {
            node('mac-slaves') {
                echo "Running MacOS specific build steps..."
                withEnv(['PLATFORM=mac']) {
                    stage("Build MacOS ${CONFIGURATION}") {
                        checkout scm
                        sh 'git submodule update --init --recursive'
                        sh 'git submodule update --recursive'
                        sh "./project/victor/scripts/victor_build_${CONFIGURATION}.sh -p ${PLATFORM}"
                    }
                    stage('Collect MacOS artifacts') {
                        archiveArtifacts artifacts: '_build/mac/**', onlyIfSuccessful: true, caseSensitive: true
                    }
                }
                stage('Webots') {
                    sh './project/build-scripts/webots/webotsTest.py'
                }
                stage('Unit Tests') {
                    sh './project/buildServer/steps/unittestsUtil.sh'
                    sh './project/buildServer/steps/unittestsCoretech.sh'
                    sh './project/buildServer/steps/unittestsEngine.sh'
                }
            }
        }
        withEnv(['PLATFORM=vicos']) {
            stage("Build Vicos ${CONFIGURATION}") {
                sh "./project/victor/scripts/victor_build_${CONFIGURATION}.sh -p ${PLATFORM}"
            }
        }
        stage('Zip Deployables') {
            sh './project/buildServer/steps/zipDeployables.sh'
        }
        stage('Collecting artifacts') {
            archiveArtifacts artifacts: '_build/**', onlyIfSuccessful: true, caseSensitive: true
        }
    }
}