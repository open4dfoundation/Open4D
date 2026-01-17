//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Framework
{
    /// <summary>
    /// Profiler class. Can be used as a static Profiler:
    /// <code>
    /// Profiler.Begin("msg");
    /// ...
    /// Profiler.End(); // DO NOT FORGET TO CALL!
    /// </code>
    /// or as an IDisposable instance:
    /// <code>
    /// using(new Profiler("msg)) {
    /// ...
    /// }
    /// </code>
    /// </summary>
    public class Profiler : IDisposable
    {
        /*static Stopwatch watch;
        static Stack<(string, long)> timeStack = new();*/
        static Stack<(string name, string param)> nameStack = new();
        static int level = 0;
        static List<ProfilerMeasurementProvider> profilerProviders = new();
        static List<ProfilerOutputProvider> profilerOutputs = new();
        string name;

        public static T AddMeasure<T>(T provider) where T : ProfilerMeasurementProvider
        {
            profilerProviders.Add(provider);
            return provider;
        }

        public static T AddOutput<T>(T output) where T : ProfilerOutputProvider
        {
            profilerOutputs.Add(output);
            return output;
        }

        public static void Init()
        {
            foreach (var output in profilerOutputs)
                output.Header();
        }

        public static void Finish()
        {
            foreach (var output in profilerOutputs)
                output.Footer();
            profilerOutputs.Clear();
            profilerProviders.Clear();
        }

        static Profiler()
        {
            /*watch = new Stopwatch();
            watch.Start();*/
        }

        public static T Profile<T>(string name, string param, Func<T> func)
        {
#pragma warning disable 0618
            Begin(name, param);
#pragma warning restore 0618
            var result = func();
            End();
            return result;
        }

        public static void Profile(string name, Action func)
        {
            Profile(name, null, func);
        }

        public static void Profile(string name, string param, Action func)
        {
#pragma warning disable 0618
            Begin(name, param);
#pragma warning restore 0618
            func();
            End();
        }

        public static T Profile<T>(string name, Func<T> func)
        {
            return Profile(name, null, func);
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        [Obsolete("Do NOT forget to call End()!\nUse safer version: \nProfiler.Profile<T>(\"message\", Func<T>)\nor slower version for large piece of code:\nusing(new Profiler(\"message\")) { ... }")]
        public static void Begin(string name) => Begin(name, null);


        [MethodImpl(MethodImplOptions.Synchronized)]
        [Obsolete("Do NOT forget to call End()!\nUse safer version: \nProfiler.Profile<T>(\"message\", Func<T>)\nor slower version for large piece of code:\nusing(new Profiler(\"message\")) { ... }")]
        public static void Begin(string name, string param)
        {
            nameStack.Push((name, param));
            foreach (var Provider in profilerProviders)
                if (!Provider.Filtered(name))
                    Provider.Begin(name);

            foreach (var output in profilerOutputs)
                if (!output.Filtered(name))
                    output.BeginRecordPrepare(level, name, param);

            foreach (var Provider in profilerProviders)
            {
                if (!Provider.Filtered(name))
                    if (Provider.LastBegin != null)
                        foreach (var output in profilerOutputs)
                            if (!output.Filtered(name))
                                output.BeginRecord(Provider);
            }

            foreach (var output in profilerOutputs)
                if (!output.Filtered(name))
                    output.BeginRecordFinish(level, name, param);

            level++;
        }

        [MethodImpl(MethodImplOptions.Synchronized)]
        private static void End()
        {
            level--;
            var (name, param) = nameStack.Pop();
            foreach (var Provider in profilerProviders)
            {
                if (!Provider.Filtered(name))
                    Provider.End();
            }

            foreach (var output in profilerOutputs)
                if (!output.Filtered(name))
                    output.EndRecordPrepare(level, name, param);

            foreach (var Provider in profilerProviders)
            {
                if (!Provider.Filtered(name))
                    if (Provider.LastEnd != null)
                        foreach (var output in profilerOutputs)
                            if (!output.Filtered(name))
                                output.EndRecord(Provider);
            }

            foreach (var output in profilerOutputs)
                if (!output.Filtered(name))
                    output.EndRecordFinish(level, name, param);

        }

        public static void End(string name)
        {
#if DEBUG
            if (nameStack.Peek().name != name)
            {
                throw new ApplicationException($"Missed Profiler End on {name}");
            }
#endif
            End();
        }

        public Profiler(string name) : this(name, null) { }

        public Profiler(string name, string param)
        {
            this.name = name;
#pragma warning disable 0618
            Begin(name, param);
#pragma warning restore 0618
        }

        public void Dispose()
        {
            End(name);
        }
    }

    /// <summary>
    /// Measurement data provider is called by the provider to obtain measurement data (eg. time, memory...)
    /// </summary>
    public abstract class ProfilerMeasurementProvider : ProfilerProvider
    {
        /// <summary>
        /// What units are used for measurement (eg. ms, MB...)
        /// </summary>
        public abstract string Units { get; }

        /// <summary>
        /// Measurement descrition
        /// </summary>
        public abstract string Description { get; }

        /// <summary>
        /// Called on begin of section
        /// </summary>
        /// <param name="name"></param>
        public abstract void Begin(string name);

        /// <summary>
        /// Called on end of section
        /// </summary>
        public abstract void End();

        /// <summary>
        /// Result of last begin call
        /// </summary>
        public abstract string LastBegin { get; }

        /// <summary>
        /// Result of last end call
        /// </summary>
        public abstract string LastEnd { get; }

        /// <summary>
        /// Result of last begin call human friendlu formaat
        /// </summary>
        public abstract string LastBeginFormatted { get; }

        /// <summary>
        /// Result of last end call in human friendly format
        /// </summary>
        public abstract string LastEndFormatted { get; }
    }

    public abstract class ProfilerOutputProvider : ProfilerProvider
    {
        public virtual void Header() { }

        public virtual void BeginRecordPrepare(int level, string name, string param) { }
        public virtual void BeginRecord(ProfilerMeasurementProvider provider) { }
        public virtual void BeginRecordFinish(int level, string name, string param) { }


        public virtual void EndRecordPrepare(int level, string name, string param) { }
        public virtual void EndRecord(ProfilerMeasurementProvider provider) { }
        public virtual void EndRecordFinish(int level, string name, string param) { }

        public virtual void Footer() { }
    }
}
