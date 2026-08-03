//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;

namespace Framework
{
    public class ProfilerConsoleOutput : ProfilerOutputProvider
    {
        public override void Header()
        {
        }

        int level;
        public override void BeginRecordPrepare(int level, string name, string param)
        {
            this.level = level;
            var indent = new string(' ', level);
            var fcolor = Console.ForegroundColor;
            Console.ForegroundColor = ConsoleColor.DarkGray;
            Console.Write($"[{DateTime.Now:yyMMdd:HH:mm:ss.ff}] ");
            Console.ForegroundColor = fcolor;
            Console.WriteLine($"{indent}{indent}{indent}Start: {name} {(string.IsNullOrEmpty(param) ? " " : $"({param}) ")}...");
        }

        public override void BeginRecord(ProfilerMeasurementProvider provider)
        {
            var indent = new string(' ', level);
            Console.WriteLine($"                     {indent}{indent}{indent}       {provider.LastBeginFormatted}");
        }

        public override void EndRecordPrepare(int level, string name, string param)
        {
            this.level = level;
            var indent = new string(' ', level);
            var fcolor = Console.ForegroundColor;
            Console.ForegroundColor = ConsoleColor.DarkGray;
            Console.ForegroundColor = ConsoleColor.DarkGray;
            Console.Write($"[{DateTime.Now:yyMMdd:HH:mm:ss.ff}] ");
            Console.ForegroundColor = fcolor;
            Console.WriteLine($"{indent}{indent}{indent}Finished: {name} {(string.IsNullOrEmpty(param) ? " " : $"({param}) ")}");
        }

        public override void EndRecord(ProfilerMeasurementProvider provider)
        {
            var indent = new string(' ', level);
            Console.WriteLine($"                     {indent}{indent}{indent}          {provider.LastEndFormatted}");
        }

        public override void Footer()
        {
        }
    }
}
