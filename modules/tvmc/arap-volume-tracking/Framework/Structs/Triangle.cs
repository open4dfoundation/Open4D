//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;

namespace Framework
{
    public struct Triangle
    {
        public int V1;
        public int V2;
        public int V3;

        public Triangle(int v1, int v2, int v3)
        {
            V1 = v1;
            V2 = v2;
            V3 = v3;
        }

        public int[] Points
        {
            get
            {
                return new int[3] { V1, V2, V3 };
            }
        }

        public int this[int index]
        {
            get
            {
                switch (index)
                {
                    case 0: return V1;
                    case 1: return V2;
                    case 2: return V3;
                    default: throw new Exception("Index out of range. Triangle have only three vertices.");
                }
            }
            set
            {
                switch (index)
                {
                    case 0: V1 = value; break;
                    case 1: V2 = value; break;
                    case 2: V3 = value; break;
                    default: throw new Exception("Index out of range. Triangle have only three vertices.");
                }
            }
        }

        #region Overriden methods of the class Object

        override public string ToString()
        {
            return String.Format("Triangle: [{0}, {1}, {2}]", V1, V2, V3);
        }

        public override bool Equals(object obj)
        {
            if (obj is Triangle t)
            {
                if (((this.V1 == t.V1) || (this.V1 == t.V2) || (this.V1 == t.V3)) &&
                    ((this.V2 == t.V1) || (this.V2 == t.V2) || (this.V2 == t.V3)) &&
                    ((this.V3 == t.V1) || (this.V3 == t.V2) || (this.V3 == t.V3)))
                    return (true);
            }
            return (false);
        }

        public override int GetHashCode()
        {
            return (this.V1 * 1000 ^ this.V2 * 100 ^ this.V3);
        }

        #endregion
    }
}
