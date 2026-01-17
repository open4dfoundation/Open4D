using System.Numerics;
using TVMEditor.Editing.AffinityCalculation;
using TVMEditor.Editing.CenterDeformation;
using TVMEditor.Editing.Looping;
using TVMEditor.Editing.SurfaceDeformation;
using TVMEditor.Editing.TransformPropagation;
using TVMEditor.Structures;

namespace TVMEditor.Editing
{
    public class MeshEditor
    {
        public IAffinityCalculation AffinityCalculation { get; set; }
        public ICenterDeformation CenterDeformation { get; set; }
        public ILooping Looping { get; set; }
        public ISurfaceDeformation SurfaceDeformation { get; set; }
        public ITransformPropagation TransformPropagation { get; set; }

        public MeshEditor(IAffinityCalculation affinityCalculation, ICenterDeformation centerDeformation, ILooping looping, 
            ISurfaceDeformation surfaceDeformation, ITransformPropagation transformPropagation)
        {
            AffinityCalculation = affinityCalculation;
            CenterDeformation = centerDeformation;
            Looping = looping;
            SurfaceDeformation = surfaceDeformation;
            TransformPropagation = transformPropagation;
        }

        public void Deform(TriangleMeshSequence sequence, Vector3[][] centers, int[] indices, 
            DualQuaternion[] transformations, int frameIndex, out TriangleMeshSequence deformedSequence, out Vector3[][] deformedCenters)
        {
            sequence = sequence.Clone();
            centers = (Vector3[][]) centers.Clone();

            var newPositions = new Vector3[indices.Length];
            for (var i = 0; i < indices.Length; i++)
            {
                newPositions[i] = transformations[i].Transform(centers[frameIndex][indices[i]]);
            }

            /*var transformations2 = new DualQuaternion[centers[0].Length];
            for (var i = 0; i < indices.Length; i++)
            {
                transformations2[indices[i]] = transformations[i];
            }
            transformations = transformations2;*/

            if (AffinityCalculation != null && AffinityCalculation.GetCentersAffinity() == null)
                AffinityCalculation.CalculateCentersAffinity(centers);

            var newCenters = CenterDeformation.DeformCenters(centers[frameIndex], indices, newPositions, ref transformations);

            sequence.Meshes[frameIndex] = SurfaceDeformation.DeformSurface(sequence.Meshes[frameIndex].Vertices, 
                sequence.Meshes[frameIndex].Faces, centers[frameIndex], newCenters, frameIndex, transformations);
            Propagate(sequence, ref centers, frameIndex, centers[frameIndex], newCenters, transformations);

            deformedCenters = centers;
            deformedSequence = sequence;
        }

        public void Propagate(TriangleMeshSequence sequence, ref Vector3[][] centers, int srcIndex, Vector3[] oldCenters, Vector3[] newCenters, DualQuaternion[] transformations)
        {
            var newPropagatedCenters = TransformPropagation.PropagateTransform(centers, srcIndex,
                oldCenters, newCenters, transformations, out var propagatedTransformations);

            for (var f = 0; f < sequence.Meshes.Length; f++)
            {
                if (srcIndex == f || !TransformPropagation.FrameIsAffected(srcIndex, f))
                    continue;

                sequence.Meshes[f] = SurfaceDeformation.DeformSurface(sequence.Meshes[f].Vertices,
                    sequence.Meshes[f].Faces, centers[f], newPropagatedCenters[f], f, propagatedTransformations[f]);

            }

            centers = newPropagatedCenters;
        }
    }
}
