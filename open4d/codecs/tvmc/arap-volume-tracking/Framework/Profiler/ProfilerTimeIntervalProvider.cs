//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System.Collections.Generic;
using System.Diagnostics;

namespace Framework
{
    public class ProfilerTimeIntervalProvider : ProfilerMeasurementProvider
    {
        public override string Units => "ms";
        public override string Description => "Duration of operation";

        Stack<(string, long)> timeStack = new();
        Stopwatch watch;
        string lastResult;

        public ProfilerTimeIntervalProvider()
        {
            watch = new Stopwatch();
            watch.Start();
        }

        public override string LastEnd => lastResult;

        public override string LastBegin => null;
        public override string LastBeginFormatted => null;

        public override string LastEndFormatted => $"in {LastEnd} ms";


        public override void Begin(string name)
        {
            var time = watch.ElapsedMilliseconds;
            timeStack.Push((name, time));
        }

        public override void End()
        {
            var timeend = watch.ElapsedMilliseconds;
            var (_, time) = timeStack.Pop();
            lastResult = $"{timeend - time}";
        }
    }
}
