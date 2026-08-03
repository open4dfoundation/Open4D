using TVMEditor.Editing;
using TVMEditor.Editing.AffinityCalculation;
using TVMEditor.Editing.CenterDeformation;
using TVMEditor.Editing.SurfaceDeformation;
using TVMEditor.Editing.TransformPropagation;
using TVMEditor.IO;

namespace TVMEditor.Test.Experiments
{
    public class VectorQuaternionPentExperiment3
    {
        public static void Run(string inputDir, string outputDir)
        {
            var sequence = MeshIO.LoadSequenceFromObj($"{inputDir}/meshes");
            var centers = CentersIO.LoadCentersFiles($"{inputDir}/centers");
            var (indices, transformations) = TransformationsIO.LoadIndexedTransformations($"{inputDir}/indices_3.txt", $"{inputDir}/transformations_3.txt");

            var affinityCalculation = new DistanceDirectionAffinityCalculation
            {
                ShapeDistance = 1f
            };
            var centersDeformations = new AffinityCenterDeformationQuaternionVector(affinityCalculation);
            var surfaceDeformation = new CustomSurfaceDeformation(affinityCalculation);
            var transformPropagation = new KabschTransformPropagation(affinityCalculation);

            var editor = new MeshEditor(affinityCalculation, centersDeformations, null, surfaceDeformation, transformPropagation);

            editor.Deform(sequence, centers, indices, transformations, 0, out var deformedSequence, out var deformedCenters);
            MeshIO.WriteSequenceToObj($"{outputDir}/pent/vq_3", deformedSequence);
        }
    }
}
