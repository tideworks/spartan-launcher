/* ReadLine.java

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
package spartan_example;

import java.io.IOException;
import java.io.Reader;
import java.nio.CharBuffer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * <b>NOTE:</b> This class is not designed nor intended to be thread re-entrant safe,
 * so use it on a single thread context.
 * <p>
 * Use this class to wrap a {@link java.io.Reader} and then be able to read a line of
 * text at a time as a CharSequence (instead of String). This avoids any memory allocation
 * (garbage generation) when consuming the the Reader source per every line read.
 * <p>
 * It is recommended that Java regular expression match objects be used to process these
 * CharSequence lines of text as its API accept CharSequence. A Matcher object can be reset
 * to each line so it too does not have to be memory allocated per each line consumed.
 */
public class ReadLine implements CharSequence {
  private static final boolean isDbg = false; // set to true to enable stdout debugging statements
  private static final String defaultEolRegEx = "\r?\n";
  private static final int defaultInternalBufferSize = 128; // bytes
  private final Reader rdr;
  private final Pattern eol;
  private final Matcher matcher;
  private final StringBuilder scratchBuf = new StringBuilder();
  private CharBuffer buffer;
  private int internalBufferSize;
  private boolean eof = false;
  private boolean eol_found = false;
  private boolean lineLookAhead = false;
  private CharSequence lastLine = null;
  private long lineNbr = 0;
  private int nextStartPos = 0;
  private int startIndex = 0;
  private int endIndex = 0;

  private boolean fillBuffer() throws IOException {
    final boolean hadRemaining = buffer.remaining() > 0;
    int savePos = buffer.position();
    if (isDbg) {
      dbgPrint(String.format(">>fillBuffer()[savPos %d]", savePos));
    }
    final int nbr = rdr.read(buffer);
    savePos = buffer.position() - savePos;
    if (savePos != 0) {
      buffer.flip();
    }
    if (eof = nbr == -1) {
      buffer.limit(buffer.position());
      buffer.position(0);
    }
    if (isDbg) {
      dbgPrint(String.format("<<fillBuffer()[savPos %d,bytes-read %d]", savePos, nbr));
    }
    return buffer.remaining() > 0 && (savePos != 0 || hadRemaining);
  }

  private void resetMatcherOnBuffer() {
    matcher.reset(buffer);
    matcher.region(buffer.position(), buffer.limit());
  }

  private void initialize(final int internalBufferSize) throws IOException {
    buffer = CharBuffer.allocate(this.internalBufferSize = internalBufferSize);
    if (fillBuffer()) {
      resetMatcherOnBuffer();
      nextStartPos = matcher.regionStart();
      eol_found = matcher.find();
    }
  }

  public ReadLine(final Reader rdr) throws IOException {
    this(rdr, defaultInternalBufferSize, defaultEolRegEx);
  }

  public ReadLine(final Reader rdr, final int internalBufferSize) throws IOException {
    this(rdr, internalBufferSize, defaultEolRegEx);
  }

  public ReadLine(final Reader rdr, final String eolRegEx) throws IOException {
    this(rdr, defaultInternalBufferSize, eolRegEx);
  }

  public ReadLine(final Reader rdr, final int internalBufferSize, final String eolRegEx) throws IOException {
    this.rdr = rdr;
    this.eol = Pattern.compile(eolRegEx);
    this.matcher = this.eol.matcher("");
    initialize(internalBufferSize);
  }

  /**
   * @return the internal buffer size - which may be what was specified
   * when {@link ReadLine} object was constructed, or may be larger if
   * the buffer was increased in size to accommodate a line of text.
   */
  public int bufferCapacity() { return buffer.capacity(); }

  /**
   * The notable difference in the getter and setter of line number for
   * the {@link ReadLine} class is that it supports long instead of just int,
   * which the {@link java.io.LineNumberReader} is limited to.
   *
   * @return current line number position in the stream being read
   */
  public long getLineNumber() { return lineNbr; }

  /**
   * @see #getLineNumber()
   */
  public void setLineNumber(final long lineNumber) { lineNbr = lineNumber; }

  private void lineNumberInc() { lineNbr++; }

  public ReadLine wasLineLookAhead() {
    lineLookAhead = true;
    return this;
  }

  private void dbgPrint(String tag) {
    if (isDbg) {
      System.out.printf("%n%s-buffer[%d]: eof %b, eol_found %b, nextStartPos %d%n\tposition %d, limit %d, remaining %d%n",
          tag, buffer.capacity(), eof, eol_found, nextStartPos, buffer.position(), buffer.limit(), buffer.remaining());
    }
  }

  /**
   * The core API method of this class - returns a line of text from the wrapped
   * {@link java.io.Reader} stream as a CharSequence. No new memory allocation is
   * done to return this CharSequence but instead it is an interface on the ReadLine
   * object class itself.
   *
   * @return line of text from Reader stream as a CharSequence
   * @throws IOException
   */
  public CharSequence readLine() throws IOException {
    if (lineLookAhead) {
      lineLookAhead = false;
      return lastLine;
    } else {
      return lastLine = readLineImpl();
    }
  }

  private CharSequence readLineImpl() throws IOException {
    for(;;) {
      if (eol_found) {
        final CharSequence line = slice(nextStartPos, matcher.start());
        nextStartPos = matcher.end();
        eol_found = matcher.hitEnd() ? false : nextStartPos < matcher.regionEnd() ? matcher.find() : false;
        lineNumberInc();
        return line;
      } else {
        buffer.position(nextStartPos);
        final int remaining = buffer.remaining();
        if (eof) {
          if (remaining > 0) {
            nextStartPos = buffer.limit();
            lineNumberInc();
            dbgPrint("AAA");
            return slice(buffer.position(), nextStartPos);
          }
          dbgPrint("BBB");
        } else {
          if (remaining <= 0) {
            buffer.clear();
            dbgPrint("CCC");
          } else if (remaining > 0 && remaining < buffer.capacity()) {
            dbgPrint(">>DDD");
            buffer.compact();
            dbgPrint("<<DDD");
          } else { // remaining == capacity
            final CharBuffer prevBuffer = buffer;
            buffer = CharBuffer.allocate(internalBufferSize *= 2); // double buffer size
            buffer.put(prevBuffer);
            dbgPrint("EEE");
          }
          if (fillBuffer()) {
            resetMatcherOnBuffer();
            nextStartPos = matcher.regionStart();
            eol_found = matcher.find();
            dbgPrint("FFF");
            continue;
          }
        }
      }
      break;
    }
    dbgPrint("GGG");
    buffer.position(nextStartPos = buffer.limit());
    dbgPrint("HHH");
    this.startIndex = this.endIndex = 0;
    return null;
  }

  private ReadLine slice(final int startIndex, final int endIndex) {
    assert(startIndex <= endIndex && endIndex >= 0);
    this.startIndex = startIndex;
    this.endIndex = endIndex;
    return this;
  }

  /* (non-Javadoc)
   * @see java.lang.CharSequence#length()
   */
  @Override
  public int length() { return endIndex - startIndex; }

  /* (non-Javadoc)
   * @see java.lang.CharSequence#charAt(int)
   */
  @Override
  public char charAt(int index) {
    index += startIndex;
    assert(index < endIndex);
    return buffer.get(index);
  }

  /* (non-Javadoc)
   * @see java.lang.CharSequence#subSequence(int, int)
   *
   * Not thread reentrant safe.
   */
  @Override
  public CharSequence subSequence(int start, int end) {
    final int len = end - start;
    if (len == 0) return "";
    if (!((start + startIndex) < endIndex && (end + startIndex) <= endIndex)) {
      final CharSequence cs = buffer.subSequence(startIndex, endIndex);
      final String m = String.format("%nstartIndex: %d, endIndex: %d, len: %d%nstart: %d, end: %d, len: %d%n%s",
          startIndex, endIndex, endIndex - startIndex, start, end, end - start, cs);
      throw new AssertionError(m);
    }
    if (len > 0) {
      scratchBuf.setLength(0);
      scratchBuf.ensureCapacity(len);
      return scratchBuf.append((CharSequence) this, start, end).toString();
    } else {
      return "";
    }
  }

//  public CharSequence subSequence(int start, int end) {
//    start += startIndex;
//    end += startIndex;
//    assert(start < endIndex && end <= endIndex);
//    return buffer.subSequence(start, end);
//  }

  /* (non-Javadoc)
   * @see java.lang.Object#toString()
   *
   * Note thread reentrant safe.
   */
  @Override
  public String toString() {
    final int len = length();
    if (len > 0) {
      scratchBuf.setLength(0);
      scratchBuf.ensureCapacity(len);
      return scratchBuf.append((CharSequence) this).toString();
    } else {
      return "";
    }
  }
}
