/* SpartanSysLogAppender.java

Copyright 2015 - 2016 Tideworks Technology
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
package spartan;

import static spartan.Spartan.LL_ERR;
import static spartan.SpartanBase.log;

import java.io.ByteArrayOutputStream;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.OutputStreamAppender;

/**
 * This logging appender logs only ERROR level logging events
 * to the underlying OS syslog API. Any other level logging
 * events are simply ignored. (Should instead rely on a file
 * appender, etc., to log general logging events.)
 * 
 * The intent is that a monitoring tool, such as Swatch, will
 * be in use to monitor the syslog for application hard-errors
 * and raise alerts per such. Hence only application hard-errors
 * are directed to syslog via this appender.
 */
public class SpartanSysLogAppender extends OutputStreamAppender<ILoggingEvent> {
  protected final ByteArrayOutputStream memOutputStream = new ByteArrayOutputStream();
  @Override
  public void start() {
    setOutputStream(memOutputStream);
    super.start();
  }
  @Override
  protected void subAppend(ILoggingEvent event) {
    if (event.getLevel() != Level.ERROR) return;
    memOutputStream.reset();
    super.subAppend(event);
    if (memOutputStream.size() > 0) {
      // invokes the spartan.SpartanBase.log() static method - for error level will log to syslog
      final String msg = memOutputStream.toString().trim();
      log(LL_ERR, ()->msg);
    }
  }
}
