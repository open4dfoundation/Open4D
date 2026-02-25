//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

#if DEBUG
#define GLDEBUG
#endif
using OpenTK.Graphics.OpenGL4;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace Client.GLCompute
{
    /// <summary>
    /// Simple OpenTK computation windows running in standalon thread.
    /// To invoke some OpenGL calls, use <see cref="Invoke(Action)"/> or <see cref="InvokeAsync(Action)"/> methods.
    /// The windows should be closed by callig <see cref="Stop"/>
    /// </summary>
    public class ComputeWindow : GameWindow
    {
        ulong planned = 0;
        ulong finished = 0;
        Thread _renderThread;
        Thread _eventThread;
        bool _eventThreadRun = true;
        readonly BlockingCollection<Action> actionQueue = new();
        public ComputeWindow(GameWindowSettings gameWindowSettings, NativeWindowSettings nativeWindowSettings) : base(gameWindowSettings, nativeWindowSettings) { }

        public ComputeWindow() :
            base(
                GameWindowSettings.Default,
                new NativeWindowSettings()
                {
                    Size = new OpenTK.Mathematics.Vector2i(512, 512),
                    Flags = ContextFlags.Debug,
                }
            )
        {

        }

        static ComputeWindow _instance;
        public static ComputeWindow Instance()
        {
            if (_instance == null)
            {
                _instance = new ComputeWindow();
                _instance.Run();
            }
            return _instance;
        }

#if DEBUG
        DebugProc debugProc;
#endif

        static void DebugProc(DebugSource source, DebugType type, int id, DebugSeverity severity, int length, IntPtr message, IntPtr userparam)
        {
            var color = Console.ForegroundColor;
            var bcolor = Console.BackgroundColor;
            switch (type)
            {
                case DebugType.DebugTypeMarker: Console.ForegroundColor = ConsoleColor.Cyan; break;
                case DebugType.DebugTypePerformance: Console.ForegroundColor = ConsoleColor.Yellow; break;
                case DebugType.DebugTypeError: Console.ForegroundColor = ConsoleColor.Red; break;
            }

            switch (severity)
            {
                case DebugSeverity.DebugSeverityHigh: Console.BackgroundColor = ConsoleColor.DarkRed; break;
                case DebugSeverity.DebugSeverityMedium: Console.BackgroundColor = ConsoleColor.DarkYellow; break;
            }
            Console.Write($"[{severity}] ");
            Console.ForegroundColor = color;
            Console.BackgroundColor = bcolor;
            Console.WriteLine($"{Marshal.PtrToStringAnsi(message, length)}");

        }

        long lastSleep;
        Stopwatch watch;

        /// <summary>
        /// Runs the compute window.
        /// </summary>
        public override void Run()
        {
            Context.MakeCurrent();

#if GLDEBUG
            debugProc = DebugProc;
            int[] ids = null;
            GL.Enable(EnableCap.DebugOutput);
            GL.Enable(EnableCap.DebugOutputSynchronous);
            GL.DebugMessageControl(DebugSourceControl.DontCare, DebugTypeControl.DontCare, DebugSeverityControl.DontCare, 0, ids, false);
            GL.DebugMessageControl(DebugSourceControl.DontCare, DebugTypeControl.DontCare, DebugSeverityControl.DebugSeverityMedium, 0, ids, true);
            GL.DebugMessageControl(DebugSourceControl.DontCare, DebugTypeControl.DontCare, DebugSeverityControl.DebugSeverityHigh, 0, ids, true);
            GL.DebugMessageControl(DebugSourceControl.DebugSourceApplication, DebugTypeControl.DontCare, DebugSeverityControl.DontCare, 0, ids, true);
            GL.DebugMessageCallback(debugProc, IntPtr.Zero);
            //GL.DebugMessageControl(DebugSourceControl.DontCare, DebugTypeControl.DontCare, DebugSeverityControl.DebugSeverityMedium, 0, ref ids, true);
#endif
            OnResize(new ResizeEventArgs(Size));

            Context.MakeNoneCurrent();

            _renderThread = new Thread(StartRenderThread);
            _renderThread.Start();

            _eventThread = new Thread(EventThread);
            _eventThread.Start();
        }

        void EventThread()
        {
            Thread.Sleep(2000);
            while (_eventThreadRun)
            {
                InvokeAsync(EmptyAction);
                Thread.Sleep(500);
            }
            actionQueue.CompleteAdding();
        }

        void EmptyAction() { }

        /// <summary>
        /// Invokes an asynchronous OpenGL action.
        /// </summary>
        /// <param name="action">Action</param>
        public void InvokeAsync(Action action)
        {
            if (lastSleep + 2000 < watch.ElapsedMilliseconds)
            {
                GLCompute.ComputeWindow.Instance().ProcessEvents(1);
                lastSleep = watch.ElapsedMilliseconds;
            }
            ++planned;
            actionQueue.Add(action);
        }

        /// <summary>
        /// Invokes an OpenGL action and waits till it's finished.
        /// </summary>
        /// <param name="action">Action</param>
        public void Invoke(Action action)
        {
            ++planned;
            ulong thisplan = planned;
            actionQueue.Add(action);
            while (thisplan > finished)
            {
                System.Threading.Thread.Sleep(1); // HACK: this could be done in a better way
            }
        }

        /// <summary>
        /// Invokes a stop action, which finishes the rendering loop.
        /// </summary>
        public void Stop() => _eventThreadRun = false;

        private void StartRenderThread()
        {

            watch = new Stopwatch();
            watch.Start();
            lastSleep = watch.ElapsedMilliseconds;

            Context.MakeCurrent();
            foreach (var action in actionQueue.GetConsumingEnumerable())
            {
                if (lastSleep + 1500 < watch.ElapsedMilliseconds)
                {
                    GL.Flush();
                    ProcessEvents();
                    Thread.Sleep(10);
                    lastSleep = watch.ElapsedMilliseconds;
                }
                action();
                finished++;
            }
            this.Close();
        }
    }
}
