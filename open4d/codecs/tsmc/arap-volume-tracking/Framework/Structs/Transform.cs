//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using MathNet.Numerics.LinearAlgebra;
using System.Numerics;
using MathNetVector = MathNet.Numerics.LinearAlgebra.Vector<float>;

namespace Framework
{


    public class Transform
    {
        public readonly Matrix<float> R;
        public readonly MathNetVector t;

        public Transform(Matrix<float> R, MathNetVector t)
        {
            this.R = R;
            this.t = t;
        }

        public Transform(float[,] R, float[] t) : this(Matrix<float>.Build.DenseOfArray(R), MathNetVector.Build.DenseOfArray(t))
        {}

        public Transform(Matrix<float> R, MathNetVector a, MathNetVector b)
        {
            this.R = R;
            this.t = b - R * a;
        }

        public MathNetVector Apply(MathNetVector p)
        {
            return R * p + t;
        }

        public Vector4 Apply(Vector4 p)
        {
            MathNetVector pInner = MathNetVector.Build.Dense(3);
            pInner[0] = p.X;
            pInner[1] = p.Y;
            pInner[2] = p.Z;

            var resInner = Apply(pInner);

            return new Vector4(resInner[0], resInner[1], resInner[2], 0f);
        }

        public override string ToString()
        {
            return string.Join(" ", R.ToRowMajorArray()) + " " + string.Join(" ", t.ToArray());
        }
    }
}
