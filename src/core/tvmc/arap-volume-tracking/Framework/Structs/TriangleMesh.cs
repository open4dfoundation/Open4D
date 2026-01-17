//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Numerics;

namespace Framework
{
    /// <summary>
    /// Co-Authors: Lukáš Hruda, Jan Dvořák and The authors of the original framework
    /// </summary>
    public class TriangleMesh
    {
        private Vector4[] points;
        private Triangle[] triangles;
        private BoundingBox boundingBox;

        public static bool InsideTest2(float z, float[] depthArray)
        {
            int cnt = 0;
            for (int i = 0; i < depthArray.Length; i++)
            {
                if (depthArray[i] < z)
                    cnt++;
            }
            return (cnt % 2 != 0);
        }

        public float[] InsideTest3(Vector4 q)
        {
            List<float> depths = new List<float>();
            for (int i = 0; i < triangles.Length; i++)
            {
                Triangle t = triangles[i];
                if (PointInTriangle2(t, q))
                {
                    /*if (!pointInTriangle2(t, q))
                        Console.WriteLine("mismatch");*/
                    depths.Add(IntersectZ(t, q));
                }
            }
            //depths.Sort();
            var depthArray = depths.ToArray();
            return depthArray;
        }

        float IntersectZ(Triangle t, Vector4 q)
        {
            Vector4 v1_4 = points[t.V2] - points[t.V1];
            Vector4 v2_4 = points[t.V3] - points[t.V1];
            Vector3 v1 = new Vector3(v1_4.X, v1_4.Y, v1_4.Z);
            Vector3 v2 = new Vector3(v2_4.X, v2_4.Y, v2_4.Z);
            Vector3 n = Vector3.Cross(v1, v2);
            Vector4 p1_4 = points[t.V1];
            Vector3 p1 = new Vector3(p1_4.X, p1_4.Y, p1_4.Z);
            float d = -Vector3.Dot(p1, n);
            float z = (-d - n.X * q.X - n.Y * q.Y) / n.Z;
            return z;
        }

        bool PointInTriangle2(Triangle t, Vector4 q)
        {
            double e1x = points[t.V2].X - points[t.V1].X;
            double e1y = points[t.V2].Y - points[t.V1].Y;
            double e2x = points[t.V3].X - points[t.V1].X;
            double e2y = points[t.V3].Y - points[t.V1].Y;
            double qx = q.X - points[t.V1].X;
            double qy = q.Y - points[t.V1].Y;
            double a = e1x * e2y - e1y * e2x;
            if (Math.Abs(a) < double.Epsilon)
                return false;
            double alpha = (e1x * qy - e1y * qx) / a;
            if (alpha < 0)
                return false;
            double beta = (qx * e2y - qy * e2x) / a;
            if (beta < 0)
                return false;
            if ((1 - alpha - beta) < 0)
                return false;
            return true;
        }

        public BoundingBox BoundingBox
        {
            get
            {
                //if (boundingBox == null)
                boundingBox = new BoundingBox(this.points);
                return boundingBox;
            }
            set { boundingBox = value; }
        }

        public Vector4[] Points
        {
            get { return points; }
            set { points = value; }
        }

        public Triangle[] Triangles
        {
            get { return triangles; }
            set { triangles = value; }
        }

        public TriangleMesh()
        {
        }

        public override string ToString()
        {
            string str = this.GetType().FullName + ":\r\n";

            if (points != null)
            {
                str += "points: " + points.Length + "\r\n";
            }

            if (triangles != null)
            {
                str += "triangles: " + triangles.Length + "\r\n";
            }
            return str;
        }
    }
}
