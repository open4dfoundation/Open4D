using System.Numerics;
using TVMEditor.Editing.AffinityCalculation;
using TVMEditor.Extensions;
using TVMEditor.Structures;

namespace TVMEditor.Editing.CenterDeformation
{
    public class AffinityCenterDeformationMatrix : ICenterDeformation
    {
        public IAffinityCalculation AffinityCalculation { get; set; }

        public AffinityCenterDeformationMatrix(IAffinityCalculation affinityCalculation)
        {
            AffinityCalculation = affinityCalculation;
        }

        public Vector3[] DeformCenters(Vector3[] centers, int[] centerIndices, Vector3[] newPositions, ref DualQuaternion[] transformations)
        {
            var newCenters = new Vector3[centers.Length];
            var centersAffinity = AffinityCalculation.GetCentersAffinity();
            var newTransformations = new DualQuaternion[centers.Length];

            // Foreach center
            //Parallel.For(0, centers.Length, i =>
            for (var i = 0; i < centers.Length; i++)
            {
                // Initialize center transformation as an identitz
                var weightedDifference = VectorExtensions.Zero4x4Matrix();
                //var weightedDifference = Vector3.Zero;
                // Sum of the weights
                var weightSum = 0f;

                bool[] addedToSum = new bool[centers.Length];

                // For each center which was transfromed (effectors)
                for (var j = 0; j < centerIndices.Length; j++)
                {
                    // Index of effector
                    var centerIndex = centerIndices[j];
                    // Weight of effector
                    var w = centersAffinity[i, centerIndex];
                    // Add weighted transformation to the final transformation
                    //var wtr = (Matrix4x4.CreateTranslation(transformations[centerIndex].GetTranslation()) * Matrix4x4.CreateFromQuaternion(transformations[centerIndex].Real)).MultiplyByScalar(w); //weightedDifference.AddMatrix(transformations[centerIndex].ToMatrix().MultiplyByScalar(w));
                    var wtr = (Matrix4x4.CreateFromQuaternion(transformations[centerIndex].Real) * Matrix4x4.CreateTranslation(transformations[centerIndex].GetTranslation())).MultiplyByScalar(w);
                    weightedDifference = weightedDifference.AddMatrix(wtr); // TODO Normalize DQ
                    weightSum += w;
                    addedToSum[centerIndex] = true;
                }

                // Divide by sum of weights
                weightedDifference = weightedDifference.MultiplyByScalar(1 / weightSum);
                // weightedDifference /= newPositions.Length;
                // Normalize DQ

                // Apply transformation to original center
                newTransformations[i] = DualQuaternion.Rotation(Quaternion.CreateFromRotationMatrix(weightedDifference));  // TODO DualQuaternion.Rotation(weightedDifference.rotation);
                var trans = new Vector3(weightedDifference.GetRow(3).X, weightedDifference.GetRow(3).Y, weightedDifference.GetRow(3).Z) / weightedDifference.GetRow(3).W;
                newTransformations[i].AddTranslation(trans);
                newCenters[i] = Vector3.Transform(centers[i], weightedDifference);
            }//);

            for (var c = 0; c < centerIndices.Length; c++)
            {
                newCenters[centerIndices[c]] = newPositions[c];
                newTransformations[centerIndices[c]] = transformations[centerIndices[c]];
            }

            transformations = newTransformations;

            for (var c = 0; c < newCenters.Length; c++)
            {
                newCenters[c] = transformations[c].Transform(centers[c]);
            }

            return newCenters;
        }

        public Vector3[] DeformCenters2(Vector3[] centers, int[] centerIndices, Vector3[] newPositions, ref DualQuaternion[] transformations)
        {
            var newCenters = new Vector3[centers.Length];
            var centersAffinity = AffinityCalculation.GetCentersAffinity();
            var newTransformations = new DualQuaternion[centers.Length];

            // Foreach center
            //Parallel.For(0, centers.Length, i =>
            for (var i = 0; i < centers.Length; i++)
            {
                // Initialize center transformation as an identitz
                var weightedDifference = DualQuaternion.Zero();
                //var weightedDifference = Vector3.Zero;
                // Sum of the weights
                var weightSum = 0f;

                bool[] addedToSum = new bool[centers.Length];

                // For each center which was transfromed (effectors)
                for (var j = 0; j < centerIndices.Length; j++)
                {
                    // Index of effector
                    var centerIndex = centerIndices[j];
                    // Weight of effector
                    var w = centersAffinity[i, centerIndex];
                    // Add weighted transformation to the final transformation
                    weightedDifference += (w) * transformations[centerIndex];//+ (1 - w) * DualQuaternion.Identity(); // TODO Normalize DQ
                                                                             // Add weight to the sum
                    weightSum += (w);
                    addedToSum[centerIndex] = true;
                }

                // Divide by sum of weights
                // weightedDifference /= weightSum;
                // weightedDifference /= newPositions.Length;

                weightedDifference += (1 - weightSum) * DualQuaternion.Identity();
                // Normalize DQ
                weightedDifference = weightedDifference.Normalize();

                // Apply transformation to original center
                newTransformations[i] = weightedDifference;
                newCenters[i] = weightedDifference.Transform(centers[i]); // / weightSum;
            }//);

            for (var c = 0; c < centerIndices.Length; c++)
            {
                newCenters[centerIndices[c]] = newPositions[c];
                newTransformations[centerIndices[c]] = transformations[centerIndices[c]];
            }

            transformations = newTransformations;

            for (var c = 0; c < newCenters.Length; c++)
            {
                newCenters[c] = transformations[c].Transform(centers[c]);
            }

            return newCenters;
        }
    }
}
