//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Numerics;

namespace Framework
{
    public class KDTree
    {
        //static void SetVector4Axis(Vector4 vec, int axis, float val)
        //{
        //    switch (axis)
        //    {
        //        case 0:
        //            vec.X = val;
        //            break;
        //        case 1:
        //            vec.Y = val;
        //            break;
        //        case 2:
        //            vec.Z = val;
        //            break;
        //        case 3:
        //            vec.W = val;
        //            break;
        //    }
        //}

        static float GetVector4Axis(Vector4 vec, int axis)
        {
            switch (axis)
            {
                case 0:
                    return vec.X;
                case 1:
                    return vec.Y;
                case 2:
                    return vec.Z;
                case 3:
                    return vec.W;
                default:
                    break;
            }
            return float.NaN;
        }
        class Node
        {
            public Node left, right;
            public int point;
            public int axis;
        }

        readonly Node root;

        readonly Vector4[] pnts;

        readonly Vector4 minPoint, maxPoint;

        public KDTree(KDTree source)
        {
            this.root = source.root;
            this.pnts = source.pnts;
            this.minPoint = source.minPoint;
            this.maxPoint = source.maxPoint;
        }

        public KDTree(Vector4[] points)
        {
            this.pnts = points;
            List<int> list = new List<int>();
            for (int i = 0; i < points.Length; i++)
                list.Add(i);
            root = new Node();
            AddChildren(root, list, 0, 0);

            maxPoint = new Vector4(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity, 0);
            minPoint = new Vector4(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, 0);
            for (int i = 0; i < pnts.Length; i++)
            {
                if (pnts[i].X < minPoint.X)
                    minPoint.X = pnts[i].X;
                if (pnts[i].X > maxPoint.X)
                    maxPoint.X = pnts[i].X;
                if (pnts[i].Y < minPoint.Y)
                    minPoint.Y = pnts[i].Y;
                if (pnts[i].Y > maxPoint.Y)
                    maxPoint.Y = pnts[i].Y;
                if (pnts[i].Z < minPoint.Z)
                    minPoint.Z = pnts[i].Z;
                if (pnts[i].Z > maxPoint.Z)
                    maxPoint.Z = pnts[i].Z;
            }
        }

        float smDist, smDistSq;
        int nearest;
        Vector4 p;

        //int checkCount = 0;
        //int skipCount = 0;

        //private void CheckSubTreePlane(Vector4 n, float d, Vector4 min, Vector4 max, Node node)
        //{
        //    if (node == null)
        //        return;

        //    // compute critical points
        //    Vector4 cp1 = new Vector4();
        //    Vector4 cp2 = new Vector4();

        //    if (n.X > 0)
        //    {
        //        cp1.X = max.X;
        //        cp2.X = min.X;
        //    }
        //    else
        //    {
        //        cp1.X = min.X;
        //        cp2.X = max.X;
        //    }

        //    if (n.Y > 0)
        //    {
        //        cp1.Y = max.Y;
        //        cp2.Y = min.Y;
        //    }
        //    else
        //    {
        //        cp1.Y = min.Y;
        //        cp2.Y = max.Y;
        //    }

        //    if (n.Z > 0)
        //    {
        //        cp1.Z = max.Z;
        //        cp2.Z = min.Z;
        //    }
        //    else
        //    {
        //        cp1.Z = min.Z;
        //        cp2.Z = max.Z;
        //    }

        //    // distance at critical points
        //    float cpv1 = Vector4.Dot(n, cp1) + d;
        //    float cpv2 = Vector4.Dot(n, cp2) + d;

        //    if ((cpv1 * cpv2 < 0) || (Math.Abs(cpv1) < smDist) || (Math.Abs(cpv2) < smDist))
        //    {
        //        // cell is intersected by the plane or near it, we must check it and proceed
        //        float p = Math.Abs(Vector4.Dot(n, pnts[node.point]) + d);
        //        if (p < smDist)
        //        {
        //            if (CheckPoint(pnts[node.point]))
        //            {
        //                smDist = p;
        //                nearest = node.point;
        //            }
        //        }
        //        Vector4 newMax = max;
        //        SetVector4Axis(newMax, node.axis, GetVector4Axis(pnts[node.point], node.axis));
        //        CheckSubTreePlane(n, d, min, newMax, node.left);

        //        Vector4 newMin = min;
        //        SetVector4Axis(newMin, node.axis, GetVector4Axis(pnts[node.point], node.axis));
        //        CheckSubTreePlane(n, d, newMin, max, node.right);

        //        //checkCount++;
        //    }
        //    //else
        //    //    skipCount++;

        //}

        //Vector4 A, B, C;

        //readonly float minDist;

        //bool CheckPoint(Vector4 p)
        //{
        //    //Vector3 d = p - A;
        //    if ((p - A).LengthSquared() < minDist)
        //        return (false);
        //    //d = p - B;
        //    if ((p - B).LengthSquared() < minDist)
        //        return (false);
        //    //d = p - C;
        //    if ((p - C).LengthSquared() < minDist)
        //        return (false);
        //    return (true);
        //}

        private void CheckSubtree(Node n, List<int> indices, List<float> dist, float maxdistsq, float maxdist, Vector4 p)
        {
            if (n == null)
                return;
            //Vector3 v = p-pnts[n.point];
            //float d = v.DotProduct(v);
            float d = (p - pnts[n.point]).LengthSquared();
            float v = GetVector4Axis(p, n.axis) - GetVector4Axis(pnts[n.point], n.axis);
            if (d < maxdistsq)
            {
                indices.Add(n.point);
                dist.Add(MathF.Sqrt(d));
            }
            if (v > 0)
            {
                CheckSubtree(n.right, indices, dist, maxdistsq, maxdist, p);
                if (v < maxdist)
                    CheckSubtree(n.left, indices, dist, maxdistsq, maxdist, p);
            }
            else
            {
                CheckSubtree(n.left, indices, dist, maxdistsq, maxdist, p);
                if ((-v) < maxdist)
                    CheckSubtree(n.right, indices, dist, maxdistsq, maxdist, p);
            }
        }

        public int FindNearest(Vector4 p, out float dist)
        {
            this.p = p;
            smDist = float.MaxValue;
            smDistSq = float.MaxValue;
            nearest = -1;
            //searchSubtree(root, p);
            SearchSubtreeX(root);
            dist = smDist;
            return (nearest);
        }

        public int FindNearestWithGuess(Vector4 point, int guess)
        {
            p = point;
            smDistSq = (p - pnts[guess]).LengthSquared();
            smDist = MathF.Sqrt(smDistSq);
            nearest = guess;
            SearchSubtreeX(root);
            return (nearest);
        }

        void SearchSubtree(Node n, Vector4 p)
        {
            // node point
            Vector4 np = pnts[n.point];

            if (GetVector4Axis(p, n.axis) < GetVector4Axis(np, n.axis))
            {
                if ((GetVector4Axis(np, n.axis) - GetVector4Axis(p, n.axis)) < smDist)
                {
                    //Vector3 dif = p - np;
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDistSq = dist;
                        smDist = MathF.Sqrt(dist);
                        nearest = n.point;
                    }
                }
                if (n.left != null)
                {
                    SearchSubtree(n.left, p);
                    if (n.right != null)
                    {
                        float minDist = GetVector4Axis(np, n.axis) - GetVector4Axis(p, n.axis);
                        if (minDist < smDist)
                            SearchSubtree(n.right, p);
                    }
                }
            }
            else
            {
                if ((GetVector4Axis(p, n.axis) - GetVector4Axis(np, n.axis)) < smDist)
                {
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDist = MathF.Sqrt(dist);
                        smDistSq = dist;
                        nearest = n.point;
                    }
                }
                if (n.right != null)
                {
                    SearchSubtree(n.right, p);
                    if (n.left != null)
                    {
                        float minDist = GetVector4Axis(p, n.axis) - GetVector4Axis(np, n.axis);
                        if (minDist < smDist)
                            SearchSubtree(n.left, p);
                    }
                }
            }
        }

        void SearchSubtreeX(Node n)
        {
            // node point
            Vector4 np = pnts[n.point];

            if (p.X < np.X)
            {
                if ((np.X - p.X) < smDist)
                {
                    //Vector3 dif = p - np;
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDistSq = dist;
                        smDist = MathF.Sqrt(dist);
                        nearest = n.point;
                    }
                }
                if (n.left != null)
                {
                    SearchSubtreeY(n.left);
                }
                if (n.right != null)
                {
                    float minDist = np.X - p.X;
                    if (minDist < smDist)
                        SearchSubtreeY(n.right);
                }
            }
            else
            {
                if ((p.X - np.X) < smDist)
                {
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDist = MathF.Sqrt(dist);
                        smDistSq = dist;
                        nearest = n.point;
                    }
                }
                if (n.right != null)
                {
                    SearchSubtreeY(n.right);
                }
                if (n.left != null)
                {
                    float minDist = p.X - np.X;
                    if (minDist < smDist)
                        SearchSubtreeY(n.left);
                }
            }
        }

        void SearchSubtreeY(Node n)
        {
            // node point
            Vector4 np = pnts[n.point];

            if (p.Y < np.Y)
            {
                if ((np.Y - p.Y) < smDist)
                {
                    //Vector3 dif = p - np;
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDistSq = dist;
                        smDist = MathF.Sqrt(dist);
                        nearest = n.point;
                    }
                }
                if (n.left != null)
                {
                    SearchSubtreeZ(n.left);
                }
                if (n.right != null)
                {
                    float minDist = np.Y - p.Y;
                    if (minDist < smDist)
                        SearchSubtreeZ(n.right);
                }
            }
            else
            {
                if ((p.Y - np.Y) < smDist)
                {
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDist = MathF.Sqrt(dist);
                        smDistSq = dist;
                        nearest = n.point;
                    }
                }
                if (n.right != null)
                {
                    SearchSubtreeZ(n.right);
                }
                if (n.left != null)
                {
                    float minDist = p.Y - np.Y;
                    if (minDist < smDist)
                        SearchSubtreeZ(n.left);
                }
            }
        }

        void SearchSubtreeZ(Node n)
        {
            // node point
            Vector4 np = pnts[n.point];

            if (p.Z < np.Z)
            {
                if ((np.Z - p.Z) < smDist)
                {
                    //Vector3 dif = p - np;
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDistSq = dist;
                        smDist = MathF.Sqrt(dist);
                        nearest = n.point;
                    }
                }
                if (n.left != null)
                {
                    SearchSubtreeX(n.left);
                }
                if (n.right != null)
                {
                    float minDist = np.Z - p.Z;
                    if (minDist < smDist)
                        SearchSubtreeX(n.right);
                }
            }
            else
            {
                if ((p.Z - np.Z) < smDist)
                {
                    //Vector3 dif = p.Subtract(np);
                    //float dist = dif.DotProduct(dif);
                    float dist = (p - np).LengthSquared();
                    if (dist < smDistSq)
                    {
                        smDist = MathF.Sqrt(dist);
                        smDistSq = dist;
                        nearest = n.point;
                    }
                }
                if (n.right != null)
                {
                    SearchSubtreeX(n.right);
                }
                if (n.left != null)
                {
                    float minDist = p.Z - np.Z;
                    if (minDist < smDist)
                        SearchSubtreeX(n.left);
                }
            }
        }

        private void AddChildren(Node n, List<int> list, int axis, int depth)
        {
            n.axis = axis;

            if (list.Count == 1)
            {
                n.point = list[0];
                return;
            }

            int med = Median(list, axis);
            n.point = list[med];

            //float medVal = getVector4Axis(pnts[list[med]], axis);

            List<int> left = new List<int>();
            List<int> right = new List<int>();

            /*for (int i = 0; i < list.Count; i++)
            {
                if (pnts[list[i]][axis] < medVal)
                    left.Add(list[i]);
                else// if (pnts[list[i]][axis] > pnts[list[med]][axis])
                {
                    if (i!=med)
                        right.Add(list[i]);
                }
            }*/

            for (int i = 0; i < list.Count / 2; i++)
                if (i != med)
                {
                    left.Add(list[i]);
                    //if (pnts[list[i]][axis] > medVal)
                    //    Console.WriteLine("Damnit");
                }
            for (int i = list.Count / 2; i < list.Count; i++)
                if (i != med)
                {
                    right.Add(list[i]);
                    //if (pnts[list[i]][axis] < medVal)
                    //    Console.WriteLine("Damnit");
                }


            if (left.Count > 0)
            {
                n.left = new Node();
                AddChildren(n.left, left, (axis + 1) % 3, depth + 1);
            }
            if (right.Count > 0)
            {
                n.right = new Node();
                AddChildren(n.right, right, (axis + 1) % 3, depth + 1);
            }
        }

        public int Median(List<int> points, int axis)
        {
            return (FindMedian(points, 0, points.Count - 1, axis, 0));
        }

        readonly Random rnd = new Random(0);

        private int FindMedian(List<int> points, int min, int max, int axis, int depth)
        {
            if (depth > 35)
            {
                //Console.WriteLine("In too deep");
                /*bool allSame = true;
                for (int i = min + 1; i <= max; i++)
                    if (pnts[points[i]][axis] != pnts[points[min]][axis])
                        allSame = false;
                if (allSame)
                    return (min);*/
                return min;
            }
            if (min == max)
                return (min);
            if (min == (max - 1))
            {
                if (GetVector4Axis(pnts[points[min]], axis) > GetVector4Axis(pnts[points[max]], axis))
                {
                    int tmp = points[min];
                    points[min] = points[max];
                    points[max] = tmp;
                }
                return (points.Count / 2);
            }
            int pivot = min + rnd.Next(max - min + 1);
            float pivotVal = GetVector4Axis(pnts[points[pivot]], axis);
            int l = min;
            int r = max;
            while (l < r)
            {
                while (GetVector4Axis(pnts[points[l]], axis) < pivotVal)
                    l++;
                while (GetVector4Axis(pnts[points[r]], axis) >= pivotVal)
                {
                    r--;
                    if (r < 0)
                        break;
                }
                if (l < r)
                {
                    int tmp = points[l];
                    points[l] = points[r];
                    points[r] = tmp;
                    //l++;
                    //r--;
                }
            }
            if (r < (points.Count / 2))
            {
                /*for (int i = l; i <= max; i++)
                    if (pnts[points[i]][axis] < pivotVal)
                        Console.WriteLine("Damnit");*/
                return (FindMedian(points, l, max, axis, depth + 1));
            }
            else
            {
                /*for (int i = min; i <= r; i++)
                    if (pnts[points[i]][axis] > pivotVal)
                        Console.WriteLine("Damnit");*/
                return (FindMedian(points, min, r, axis, depth + 1));
            }
        }
    }
}
