//
// Copyright (c) 2022,2023 Jan Dvoøák, Zuzana Káèereková, Petr Vanìèek, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System.Numerics;

namespace Framework
{
    /// <summary>
    /// Represent min-max box.
    /// It is automatically maintained by given point array.
    /// </summary>
    public sealed class BoundingBox
    {
        private bool isMinMaxVaild;

        ///// <summary>
        ///// Number of points with maximum dimension
        ///// </summary>
        //private int maxDimCnt;

        ///// <summary>
        ///// Current maximum dimension
        ///// </summary>
        //private int maxDim;

        private Vector4 maxPoint;

        public Vector4 MaxPoint
        {
            get { return maxPoint; }
            set { maxPoint = value; }
        }
        private Vector4 minPoint;

        public Vector4 MinPoint
        {
            get { return minPoint; }
            set { minPoint = value; }
        }
        private Vector4 midPoint;

        public Vector4 MidPoint
        {
            get { return midPoint; }
            set { midPoint = value; }
        }
        private float maxDiag;

        private readonly Vector4[] points;

        /// <summary>
        /// Creates bounding box that is automatically maintained with respect to given array.
        /// </summary>
        /// <param name="points"></param>
        public BoundingBox(Vector4[] points)
        {
            this.points = points;
            this.ValidateMinMax();
            this.ValidateMidDiag();
        }

        /// <summary>
        /// Returns point array that is managed by curent bounding box.
        /// </summary>
        public Vector4[] Points
        {
            get
            {
                return this.points;
            }
        }

        /// <summary>
        /// Recomputes the BoundingBox from all points
        /// </summary>
        private void ValidateMinMax()
        {
            minPoint = points[0];
            MaxPoint = points[0];

            for (int i = 1; i < points.Length; i++)
            {
                Vector4 point = points[i];

                minPoint = Vector4.Min(minPoint, point);
                MaxPoint = Vector4.Max(MaxPoint, point);
            }
            isMinMaxVaild = true;
            this.ValidateMidDiag();
        }

        /// <summary>
        /// Recompute mid-point and max diagonal length.
        /// </summary>
        private void ValidateMidDiag()
        {
            midPoint = (minPoint + maxPoint) * 0.5f;
            Vector4 diag = maxPoint - minPoint;
            this.maxDiag = diag.Length();
        }

        /// <summary> 
        /// Return volume diagonal of the bounding box.
        /// </summary> 
        public float MaxDiag
        {
            get
            {
                if (!isMinMaxVaild)
                    this.ValidateMinMax();

                return maxDiag;
            }
        }

        /// <summary>
        /// Returns description of BoundignBox.
        /// </summary>
        public override string ToString()
        {
            string str = "BoundingBox: ";

            //if (this.MinPoint == null || this.MaxPoint == null || this.MidPoint == null)
            //    return str += "undefined (there are no points)";

            str += "\n";
            str += "\t" + "- min point : " + this.MinPoint + "\n";
            str += "\t" + "- max point : " + this.MaxPoint + "\n";
            str += "\t" + "- mid point : " + this.MidPoint + "\n";
            str += "\t" + "- max diag  : " + this.MaxDiag + "\n";
            return str;
        }

    } // class BoundingBox
} // namespace
