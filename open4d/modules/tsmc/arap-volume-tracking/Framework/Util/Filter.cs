//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace Framework
{
    public class Filter
    {
        int[] map;
        readonly int reducedNumber;
        readonly int n;

        public int Reduced { get { return reducedNumber; } }

        public Filter(string fn, int n) : this(GetDeleted(fn), n){}

        public Filter(HashSet<int> deleted, int n) {
            this.n = n;
            reducedNumber = n - deleted.Count;
            PrepareData(deleted, n);
        }

        #region Initialization
        private void PrepareData(HashSet<int> deleted, int n)
        {
            map = new int[n];

            int index = 0;
            for (int i = 0; i < n; i++)
            {
                if (deleted.Contains(i))
                {
                    map[i] = -1;
                }
                else
                {
                    map[i] = index++;
                }
            }
        }

        private static HashSet<int> GetDeleted(string fn)
        {
            HashSet<int> deleted = new();
            StreamReader sr;
            try
            {
                sr = new StreamReader(fn);
            }
            catch (FileNotFoundException e)
            {
                throw new Exception(e.Message);
            }

            string line = sr.ReadLine();

            while (line != null)
            {
                line = line.Trim();
                deleted.Add(int.Parse(line));
                line = sr.ReadLine();
            }

            return deleted;
        }
        #endregion

        #region Filtering
        public Vector4[] FilterPointCloud(Vector4[] pc)
        {
            Vector4[] result = new Vector4[reducedNumber];

            int index = 0;
            for (int i = 0; i < n; i++)
            {
                if (map[i] != -1)
                {
                    result[index++] = pc[i];
                }
            }

            return result;
        }

        public void FilterWeights(SmoothWeights weights)
        {
            float[][] reducedWeights = new float[reducedNumber][];
            List<int>[] nn = new List<int>[reducedNumber];

            for (int i = 0; i < n; i++)
            {
                int idx = map[i];
                if (idx == -1)
                    continue;


                reducedWeights[idx] = new float[reducedNumber];
                nn[idx] = new();

                for (int j = 0; j < n; j++)
                {
                    if (map[j] != -1)
                    {
                        reducedWeights[idx][map[j]] = weights.weights[i][j];
                    }
                }

                foreach (int neigh in weights.nn[i])
                {
                    if (map[neigh] != -1)
                    {
                        nn[idx].Add(map[neigh]);
                    }
                }
            }

            weights.n = reducedNumber;
            weights.weights = reducedWeights;
            weights.nn = nn;
        }
        #endregion
    }
}
