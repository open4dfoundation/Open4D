//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;

namespace Framework
{

    public class ProfilerTotalTimeProvider : ProfilerMeasurementProvider
    {
        public override string Units => "s";
        public override string Description => "Time since beginning";


        string lastBegin;
        public override string LastBegin => lastBegin;

        string lastEnd;
        public override string LastEnd => lastEnd;

        public override string LastBeginFormatted => $"at {LastBegin}s since start";

        public override string LastEndFormatted => $"at {LastEnd}s since start";

        DateTime start;

        public ProfilerTotalTimeProvider()
        {
            start = DateTime.Now;
        }

        public override void Begin(string name)
        {
            lastBegin = $"{(DateTime.Now - start).TotalSeconds:0.#}";
        }

        public override void End()
        {
            lastEnd = $"{(DateTime.Now - start).TotalSeconds:0.#}";
        }
    }
}
