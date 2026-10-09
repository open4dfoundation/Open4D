using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using TVMEditor.Editing.AffinityCalculation;
using TVMEditor.Editing.CenterDeformation;
using TVMEditor.Editing.SurfaceDeformation;
using TVMEditor.Structures;

sealed class TestAffinity : IAffinityCalculation
{
    float[,] affinity;
    public TestAffinity(float[,] value) { affinity = value; }
    public float[,] CalculateCentersAffinity(Vector3[][] centers) { return affinity; }
    public float[,] GetCentersAffinity() { return affinity; }
    public void SetCentersAffinity(float[,] value) { affinity = value; }
}

static class EditorNumerics
{
    static readonly Vector3[] Vertices = {
        new Vector3(1000f, 1200f, 900f), new Vector3(1010f, 1201f, 901f),
        new Vector3(1005f, 1210f, 905f)
    };
    static readonly Face[] Faces = {new Face {V1 = 0, V2 = 1, V3 = 2}};

    static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    static Vector3[] ManyCenters()
    {
        return Enumerable.Range(0, 40).Select(i => new Vector3(
            1000f + 100f * (i % 5), 1200f + 100f * ((i / 5) % 4), 900f + 100f * (i / 20)
        )).ToArray();
    }

    static void Deform(Vector3[] centers, bool denseAffinity, Vector3 translation)
    {
        var affinity = new float[centers.Length, centers.Length];
        if (denseAffinity)
            for (int i = 0; i < centers.Length; i++)
                for (int j = 0; j < centers.Length; j++) affinity[i, j] = 1f;
        var deformation = new CustomSurfaceDeformation(new TestAffinity(affinity));
        var transforms = centers.Select(c => DualQuaternion.Translation(translation)).ToArray();
        var moved = centers.Select(c => c + translation).ToArray();
        var result = deformation.DeformSurface(Vertices, Faces, centers, moved, 0, transforms);
        Require(result.Vertices.Length == Vertices.Length && result.Faces.Length == Faces.Length,
                "Deformation changed the mesh's element counts");
        for (int i = 0; i < Vertices.Length; i++)
        {
            var actual = result.Vertices[i];
            Require(float.IsFinite(actual.X) && float.IsFinite(actual.Y) && float.IsFinite(actual.Z),
                    "Surface deformation produced nonfinite coordinates");
            Require(Vector3.Distance(actual, Vertices[i] + translation) < 0.01f,
                    "Blended identical transformations failed to preserve the expected translation");
        }
        Require(result.Faces[0].V1 == 0 && result.Faces[0].V2 == 1 && result.Faces[0].V3 == 2,
                "Surface deformation changed connectivity");
    }

    static void EmptyCenters()
    {
        try { Deform(Array.Empty<Vector3>(), false, Vector3.Zero); }
        catch (ArgumentException) { return; }
        catch (AggregateException error)
        {
            if (error.Flatten().InnerExceptions.All(e => e is ArgumentException)) return;
            throw;
        }
        throw new Exception("Empty deformation centers were accepted");
    }

    static void FewCenters()
    {
        Deform(new[] {Vertices[0]}, false, new Vector3(1f, 2f, 3f));
        Deform(new[] {Vertices[0], Vertices[1]}, false, new Vector3(1f, 2f, 3f));
    }

    static void Effectors(bool partial)
    {
        var centers = ManyCenters();
        var affinity = new float[centers.Length, centers.Length];
        for (int i = 0; i < centers.Length; i++) affinity[i, i] = 1f;
        var deformation = new AffinityCenterDeformation(new TestAffinity(affinity));
        var indices = partial ? new[] {0, 2} : Array.Empty<int>();
        var transforms = new DualQuaternion[centers.Length];
        var first = new Vector3(1f, 2f, 3f);
        var second = new Vector3(-2f, 1f, -4f);
        if (partial)
        {
            transforms[0] = DualQuaternion.Translation(first);
            transforms[2] = DualQuaternion.Translation(second);
        }
        var moved = indices.Select(i => transforms[i].Transform(centers[i])).ToArray();
        var result = deformation.DeformCenters(centers, indices, moved, ref transforms);
        for (int i = 0; i < centers.Length; i++)
        {
            var offset = partial && i == 0 ? first : partial && i == 2 ? second : Vector3.Zero;
            Require(Vector3.Distance(result[i], centers[i] + offset) < 0.01f,
                    "Centers without effector influence must stay unchanged");
            Require(float.IsFinite(transforms[i].Norm()) && Math.Abs(transforms[i].Norm() - 1f) < 0.00001f,
                    "Center deformation produced a nonfinite or invalid unit transformation");
        }
    }

    static void InvalidAffinity()
    {
        foreach (var invalid in new[] {-1f, float.NaN, float.PositiveInfinity})
        {
            var affinity = new float[,] {{invalid}};
            var deformation = new AffinityCenterDeformation(new TestAffinity(affinity));
            var transformations = new[] {DualQuaternion.Identity()};
            try
            {
                deformation.DeformCenters(new[] {Vertices[0]}, new[] {0},
                                          new[] {Vertices[0]}, ref transformations);
            }
            catch (ArgumentException) { continue; }
            throw new Exception("Invalid total affinity was accepted");
        }
    }

    public static int Main(string[] arguments)
    {
        var cases = new Dictionary<string, Action> {
            {"zero_affinity", () => Deform(ManyCenters(), false, Vector3.Zero)},
            {"few_centers", FewCenters},
            {"duplicate_centers", () => Deform(new[] {Vertices[0], Vertices[0], Vertices[1], Vertices[1]}, false, Vector3.Zero)},
            {"empty_centers", EmptyCenters},
            {"finite_deformation", () => Deform(ManyCenters(), true, new Vector3(3f, -4f, 2f))},
            {"partial_effectors", () => Effectors(true)}, {"empty_effectors", () => Effectors(false)},
            {"invalid_affinity", InvalidAffinity}
        };
        cases[arguments[0]]();
        Console.WriteLine(arguments[0] + ": passed");
        return 0;
    }
}
