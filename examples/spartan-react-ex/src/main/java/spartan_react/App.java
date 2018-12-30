/* App.java

Copyright 2018 Tideworks Technology
Author: Roger D. Voss

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/
package spartan_react;


import spartan.Spartan;
import spartan.SpartanBase;
import spartan.annotations.ChildWorkerCommand;
import spartan.annotations.SupervisorCommand;
import spartan.annotations.SupervisorMain;

public class App extends SpartanBase {
  private static final String clsName = App.class.getName();
  private static final Set<Integer> _pids = ConcurrentHashMap.newKeySet(53);

  @SupervisorMain
  public static void main(String[] args) {
    log(LL_INFO, () -> format("%s: hello world - supervisor service has started!%n", programName));

    // TODO: do one time service initialization here

    enterSupervisorMode(_pids);

    log(LL_INFO, () -> format("%s exiting normally", programName));
  }
}
