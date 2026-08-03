using System;
using TVMEditor.Editing;
using TVMEditor.Editing.AffinityCalculation;
using TVMEditor.Editing.CenterDeformation;
using TVMEditor.Editing.SurfaceDeformation;
using TVMEditor.Editing.TransformPropagation;
using TVMEditor.IO;
using TVMEditor.Structures;

namespace TVMEditor.Test.Experiments
{
    public class Drinking
    {
        public static void Run(string inputDir, string outputDir)
        {
            Console.WriteLine(inputDir);
            Console.WriteLine(outputDir);
            var sequence = MeshIO.LoadSequenceFromObj($"{inputDir}/meshes");
            var centers = CentersIO.LoadCentersFiles($"{inputDir}/centers");

            /*
            for (int index = 0; index < 10; index++)
            {
                Console.WriteLine($"Dealing with index={index}");
                var (indices, transformations) = TransformationsIO.LoadIndexedTransformations($"{inputDir}/indices_{index:000}.txt", $"{inputDir}/transformations_{index:000}.txt");
                var affinityCalculation = new DistanceDirectionAffinityCalculation
                {
                    ShapeDistance = 1f
                };
                var centersDeformations = new AffinityCenterDeformation(affinityCalculation);
                var surfaceDeformation = new CustomSurfaceDeformation(affinityCalculation);
                var transformPropagation = new KabschTransformPropagation(affinityCalculation);

                var editor = new MeshEditor(affinityCalculation, centersDeformations, null, surfaceDeformation, transformPropagation);
                editor.Deform(sequence, centers, indices, transformations, index, out var deformedSequence, out var deformedCenters);
                MeshIO.WriteMeshToObj($"{outputDir}/Thomas/output/deformed_{index:000}.obj", deformedSequence.Meshes[index]);
                //MeshIO.WriteSequenceToObj($"{outputDir}/Dancer/dq", deformedSequence);
            }
            */



            Console.WriteLine("Reference mesh deformation...");
            var reference_sequence = MeshIO.LoadSequenceFromObj($"{inputDir}/reference_mesh");
            var reference_centers = CentersIO.LoadCentersFiles($"{inputDir}/reference_center");
            for (int index = 1; index < 8; index++)
            {
                Console.WriteLine($"Deform Reference mesh to Drinking {index}...");
                var (indices, transformations) = TransformationsIO.LoadIndexedTransformations($"{inputDir}/indices_{index:000}.txt", $"{inputDir}/inverse_transformations_{index:000}.txt");
                var affinityCalculation = new DistanceDirectionAffinityCalculation
                {
                    ShapeDistance = 1f
                };
                var centersDeformations = new AffinityCenterDeformation(affinityCalculation);
                var surfaceDeformation = new CustomSurfaceDeformation(affinityCalculation);
                var transformPropagation = new KabschTransformPropagation(affinityCalculation);

                var editor = new MeshEditor(affinityCalculation, centersDeformations, null, surfaceDeformation, transformPropagation);
                editor.Deform(reference_sequence, reference_centers, indices, transformations, 0, out var deformedSequence, out var deformedCenters);
                MeshIO.WriteMeshToObj($"{outputDir}/drinking/reference/deformed_reference_mesh_{index:000}.obj", deformedSequence.Meshes[0]);
            }

        }
    }
}
