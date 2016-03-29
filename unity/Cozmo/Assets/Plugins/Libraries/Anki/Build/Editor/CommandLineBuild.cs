// C# build script for unity project
using UnityEditor;
using UnityEngine;
using System.Collections.Generic;
using System.Collections;
using System.Text;
using System;

namespace Anki {
  namespace Build {
    public class CommandLineBuild {

      private static readonly IDAS sDAS = DAS.GetInstance(typeof(CommandLineBuild));

      public static void Build() {
        DAS.ClearTargets();
        DAS.AddTarget(new ConsoleDasTarget());

        string[] argv = new string[0];
        try {
          argv = System.Environment.GetCommandLineArgs();
          sDAS.Debug("args: " + System.Environment.CommandLine);
          for (int k = 0; k < argv.Length; ++k) {
            string arg = argv[k];
            sDAS.Debug("arg => " + arg);
          }
        }
        catch (UnityException ex) {
          sDAS.Error(ex.Message);
        }

        string result = Builder.BuildWithArgs(argv);

        if (!String.IsNullOrEmpty(result)) {
          // If builder.Build returned a response, then there was an error.
          // Throw an exception as an attempt to ensure that Unity exits with an error code.yep
          throw new Exception(result);
        }
      }
    }
  }
}
