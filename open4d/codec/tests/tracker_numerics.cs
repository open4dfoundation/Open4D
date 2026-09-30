using System;
using System.Collections.Generic;
using System.Numerics;
using Framework;
using MathNet.Numerics.LinearAlgebra;
using FloatVector = MathNet.Numerics.LinearAlgebra.Vector<float>;

static class TrackerNumerics
{
    static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    static void Reject(Action action)
    {
        try { action(); }
        catch (ArgumentException) { return; }
        throw new Exception("Invalid input was accepted");
    }

    static void SelfDistance()
    {
        var points = new List<Vector4>();
        for (int i = 0; i < 22; i++)
            points.Add(new Vector4(1000f + i * 0.125f, 1200f + i * 0.25f, 900f + i * 0.375f, 0f));
        var distance = new TransformDistance(points);
        var rotation = Matrix<float>.Build.DenseOfArray(new float[,] {
            {0.9553365f, -0.2955202f, 0f}, {0.2955202f, 0.9553365f, 0f}, {0f, 0f, 1f}
        });
        var translation = FloatVector.Build.DenseOfArray(new float[] {33f, -52f, 7f});
        Require(distance.GetTransformDistance(rotation, rotation, translation, translation) == 0f,
                "A transform's distance to itself must be exactly zero at large coordinates");
        Require(distance.GetTransformDistance(rotation, rotation.Clone(), translation, translation.Clone()) == 0f,
                "Value-equal cloned transforms must also have zero distance");
        var origin = new TransformDistance(new[] {Vector4.Zero});
        var identity = Matrix<float>.Build.DenseIdentity(3);
        var zero = FloatVector.Build.Dense(3);
        var offset = FloatVector.Build.DenseOfArray(new float[] {3f, -4f, 0f});
        Require(origin.GetTransformDistance(identity, identity.Clone(), zero, offset) == 25f,
                "Different translations must retain their squared transform distance");
    }

    static void EmptyWeighted()
    {
        Reject(() => ARAP.GetTransform(Array.Empty<Vector4>(), Array.Empty<Vector4>(), Array.Empty<float>()));
    }

    static void ZeroWeights()
    {
        var source = new[] {Vector4.Zero};
        var target = new[] {new Vector4(1f, 2f, 3f, 0f)};
        foreach (var value in new[] {0f, -1f, float.NaN, float.PositiveInfinity})
            Reject(() => ARAP.GetTransform(source, target, new[] {value}));
        var valid = ARAP.GetTransform(source, target, new[] {1f});
        Require(Vector4.Distance(valid.Apply(source[0]), target[0]) == 0f,
                "A valid isolated point must retain its translation");
    }

    static void EmptyUnweighted()
    {
        Reject(() => ARAP.GetTransformUnweighted(new[] {Vector4.Zero}, new[] {Vector4.Zero}, new List<int>()));
    }

    static void NonfiniteKD()
    {
        foreach (var point in new[] {
            new Vector4(float.NaN, 0f, 0f, 0f),
            new Vector4(0f, float.PositiveInfinity, 0f, 0f),
            new Vector4(0f, 0f, float.NegativeInfinity, 0f)
        }) Reject(() => new KDTree(new[] {point}));
    }

    static void FiniteKD()
    {
        var points = new[] {
            new Vector4(-7f, 1f, 0f, 0f), new Vector4(2f, 8f, 4f, 0f),
            new Vector4(3f, 2f, -1f, 0f), new Vector4(1f, -9f, 1f, 0f),
            new Vector4(5f, 0f, 7f, 0f)
        };
        var tree = new KDTree(points);
        Require(tree.FindNearest(points[2], out float exact) == 2 && exact == 0f,
                "Finite exact nearest-neighbor query failed");
        Require(tree.FindNearest(points[2] + new Vector4(0.01f, 0f, 0f, 0f), out float near) == 2
                && Math.Abs(near - 0.01f) < 0.00001f, "Finite nearby query failed");
        var duplicates = new KDTree(new[] {Vector4.One, Vector4.One, Vector4.One});
        Require(duplicates.FindNearest(Vector4.One, out float same) >= 0 && same == 0f,
                "Finite duplicate coordinates must remain supported");
    }

    public static int Main(string[] arguments)
    {
        var cases = new Dictionary<string, Action> {
            {"self_distance", SelfDistance}, {"empty_weighted", EmptyWeighted},
            {"zero_weights", ZeroWeights}, {"empty_unweighted", EmptyUnweighted},
            {"nonfinite_kd", NonfiniteKD}, {"finite_kd", FiniteKD}
        };
        cases[arguments[0]]();
        Console.WriteLine(arguments[0] + ": passed");
        return 0;
    }
}
