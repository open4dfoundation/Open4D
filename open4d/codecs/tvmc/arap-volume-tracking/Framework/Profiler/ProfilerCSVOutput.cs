//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Framework
{
    public class ProfilerCSVOutput : ProfilerOutputProvider
    {
        string SEPERATOR = ";";

        Dictionary<string, string> record = new();
        string[] includes;
        string outputDir;
        string newRecord;
        Type output;
        string filename;
        StringBuilder current = new();

        public override string[] FilterIn
        {
            get { return base.FilterIn; }
            init
            {
                includes = value;
                base.FilterIn = value;
            }
        }

        /// <summary>
        /// Use FilterIn property to register all messages that have to be logged into the csv file.
        /// </summary>
        public ProfilerCSVOutput(Type profilerProvider, string newRecord, string outputDir)
        {
            this.newRecord = newRecord;
            this.outputDir = outputDir;
            this.output = profilerProvider;
            filename = outputDir + "/" + profilerProvider.Name + ".csv";
        }

        public override void Header()
        {
            foreach (var column in includes)
            {
                record.Add(column, "");
            }
            File.WriteAllText(filename, string.Join(SEPERATOR, includes) + "\n");
        }

        string currentName;
        public override void BeginRecordPrepare(int level, string name, string param)
        {
            if (name == newRecord)
            {
                foreach (var column in includes)
                {
                    record[column] = "";
                }

            }
        }

        public override void BeginRecord(ProfilerMeasurementProvider provider)
        {
        }

        bool endRecord = false;
        public override void EndRecordPrepare(int level, string name, string param)
        {
            if (name == newRecord) endRecord = true;
            currentName = name;
        }

        public override void EndRecord(ProfilerMeasurementProvider provider)
        {
            if (provider.GetType() == output)
            {
                record[currentName] = provider.LastEnd;
            }

            if (endRecord)
            {
                StringBuilder recordLine = new StringBuilder();
                bool first = true;
                foreach (var column in FilterIn)
                {
                    if (first) first = false;
                    else recordLine.Append(SEPERATOR);

                    recordLine.Append(record[column]);
                }
                endRecord = false;
                File.AppendAllText(filename, recordLine.ToString() + "\n");
            }
        }

        public override void Footer()
        {
        }
    }
}
