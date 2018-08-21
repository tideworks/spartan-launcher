/* Flow.java

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
package spartan.fstreams;

import spartan.Spartan.InvokeResponseEx;

import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BiConsumer;

/**
 * Interrelated interfaces and static methods for establishing flow-controlled components in which
 * invoked child processes are managed for consuming their generated output via a reactive programming
 * API.
 */
@SuppressWarnings("unused")
public final class Flow {
  private static final String clsName = Flow.class.getSimpleName();
  private static final AtomicInteger workerThreadNbr = new AtomicInteger(1);
  private static final ExecutorService workerThread = Executors.newCachedThreadPool(r -> {
    final Thread t = new Thread(r);
    t.setDaemon(true);
    t.setName(String.format("%s-pool-thread-#%d", clsName, workerThreadNbr.getAndIncrement()));
    return t;
  });
  private final ExecutorCompletionService<Integer> exec = new ExecutorCompletionService<>(workerThread);
  private final List<Callable<Integer>> tasksList = new ArrayList<>();
  private final BiConsumer<InputStream, Subscription> noopAction = (os, sc) -> {};

  /**
   * The subscriber interface allows for establishing callbacks (lambdas or method pointers) for
   * consuming the stdout and stderr pipe streams from the invoked child process.
   * <p>
   * The start() method
   * is used to initiate the interaction for consuming child process output.
   * <p>
   * The subscribe() method can be used to add yet another subscriber context for any additionally
   * invoked child processes (can just keep chaining them on via inline code or iteratively in a loop.
   */
  public interface Subscriber {
    Subscriber onError(BiConsumer<InputStream, Subscription> onErrorAction);
    Subscriber onNext(BiConsumer<InputStream, Subscription> onNextAction);
    Subscriber subscribe(InvokeResponseEx rsp);
    FuturesCompletion start();
  }

  /**
   * A subscription interface is used to interact with the child process - can either cancel (causing
   * the child process to be terminated via a SIGINT signal, or get an output stream to by which to
   * communicate to the child process via writing to its stdin pipe.
   */
  public interface Subscription {
    void cancel();
    OutputStream getRequestStream();
  }

  /**
   * This interface is returned by the {@link Subscriber#start()} method. Its poll() or take() methods
   * can be used to obtain the {@link Future} per each invoked child process. The count() method returns
   * how many task completion context have been queued to be harvested via poll() or take().
   * <p>
   * The {@link Future#get()} method will return the process pid number of an invoked child process. The
   * pid can be audit correlated with the {@link spartan.Spartan.InvokeResponseEx#childPID} field from when
   * the child process was invoked (i.e., check that every child process invoked has indeed completed).
   */
  public interface FuturesCompletion {
    /**
     * Retrieves and removes the Future representing the next completed task or null if none are present.
     * @return the Future representing the next completed task (or null)
     */
    Future<Integer> poll();
    /**
     * Retrieves and removes the Future representing the next completed task, waiting if necessary up to
     * the specified wait time if none are yet present.
     * @param timeout how long to wait before giving up, in units of unit
     * @param unit a TimeUnit determining how to interpret the timeout parameter
     * @return the Future representing the next completed task (or null)
     */
    Future<Integer> poll(long timeout, TimeUnit unit) throws InterruptedException;
    /**
     * Retrieves and removes the Future representing the next completed task, waiting if none are yet present.
     * @return the Future representing the next completed task
     */
    Future<Integer> take() throws InterruptedException;

    /**
     * @return the number of task to harvest Futures on
     */
    int count();
  }

  /**
   * Use this static method to initiate the first subscription context, thereafter additional
   * subscription context can be chained on via the {@link Subscriber#subscribe(InvokeResponseEx)}
   * instance method.
   * <p>
   * Or subscriptions can also be iteratively chained together in a loop where within the loop a child
   * process is invoked and its returned {@link spartan.Spartan.InvokeResponseEx} context is passed to
   * the subscription() method (the first iteration being passed to this static method).
   * @param rsp the child process context returned by {@link spartan.Spartan#invokeCommandEx(String...)}
   * @return the subscriber context which should be populated with child process stdout and stderr
   *         callback handlers (either lambdas or method pointers).
   */
  public static Subscriber subscribe(InvokeResponseEx rsp) {
    return new Flow().makeSubscriber(rsp);
  }

  private Subscriber makeSubscriber(final InvokeResponseEx rsp) {
    return new Subscriber() {
      private BiConsumer<InputStream, Subscription> onErrorAction = noopAction;
      private BiConsumer<InputStream, Subscription> onNextAction = noopAction;
      private final Subscription subscription = new Subscription() {
        @Override
        public void cancel() {
          // use Spartan API to kill child process via its pid
        }
        @Override
        public OutputStream getRequestStream() {
          return rsp.childInputStream;
        }
      };
      private void validateInit() {
        if (onErrorAction == noopAction) {
          throw new AssertionError("onErrorAction callback not initialized");
        }
        if (onNextAction == noopAction) {
          throw new AssertionError("onNextAction callback not initialized");
        }
      }
      @Override
      public Subscriber onError(BiConsumer<InputStream, Subscription> onErrorAction) {
        this.onErrorAction = onErrorAction;
        tasksList.add(() -> { onErrorAction.accept(rsp.errStream, subscription); return rsp.childPID; });
        return this;
      }
      @Override
      public Subscriber onNext(BiConsumer<InputStream, Subscription> onNextAction) {
        this.onNextAction = onNextAction;
        tasksList.add(() -> { onNextAction.accept(rsp.inStream, subscription);   return rsp.childPID; });
        return this;
      }
      @Override
      public Subscriber subscribe(InvokeResponseEx rsp2) {
        validateInit();
        if (rsp == rsp2) {
          throw new AssertionError("atttempting to subscribe with the same InvokeResponseEx instance again");
        }
        return makeSubscriber(rsp2);
      }
      public FuturesCompletion start() {
        validateInit();
        final int tasksCount = tasksList.size();
        tasksList.forEach(exec::submit);
        tasksList.clear();
        return new FuturesCompletion() {
          @Override
          public Future<Integer> poll() {
            return exec.poll();
          }
          @Override
          public Future<Integer> poll(long timeout, TimeUnit unit) throws InterruptedException {
            return exec.poll(timeout, unit);
          }
          @Override
          public Future<Integer> take() throws InterruptedException {
            return exec.take();
          }
          @Override
          public int count() {
            return tasksCount;
          }
        };
      }
    };
  }
}