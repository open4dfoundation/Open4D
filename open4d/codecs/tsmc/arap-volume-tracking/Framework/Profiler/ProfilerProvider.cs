//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System.Collections.Generic;
using System.Linq;

namespace Framework
{
    /// <summary>
    /// Abstract profiler provider common for measurement and output
    /// </summary>
    public abstract class ProfilerProvider
    {
        protected HashSet<string> filterIn = new();

        /// <summary>
        /// Measurement names that are included
        /// </summary>
        public virtual string[] FilterIn
        {
            init
            {
                filterIn.Clear();
                foreach (var s in value) filterIn.Add(s);
            }
            get
            {
                return filterIn.ToArray();
            }
        }

        protected HashSet<string> filterOut = new();

        /// <summary>
        /// Measurement names that are excluded
        /// </summary>
        public virtual string[] FilterOut
        {
            init
            {
                filterOut.Clear();
                foreach (var s in value) filterOut.Add(s);
            }
            get
            {
                return filterOut.ToArray();
            }
        }


        /// <summary>
        /// Is measurement excluded?
        /// </summary>
        /// <param name="name">Measurement name</param>
        /// <returns>Is exluded</returns>
        public bool Filtered(string name)
        {
            if (filterIn.Count != 0)
            {
                return !filterIn.Contains(name);
            }
            if (filterOut.Count != 0)
            {
                return filterOut.Contains(name);
            }
            return false;
        }

    }
}
