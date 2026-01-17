# Documentation
Library **TVMEditor** contatins implementation of proposed and reference methods, which has been used in the editing pipeline.

Names of proposed methods are bold.

## Affinity calculation
Calculates affinity between volume centers.
###Implementation
 - **DistanceDirectionAffinityCalculation.cs**

## Centers deformation
Computes deformations of centers with respect to a given transformations of the effector centers.
###Implementation
 - **AffinityCenterDeformation.cs**
 - AffinityCenterDeformationMatrix.cs
 - AffinityCenterDeformationQuaternionVector.cs
 - AffinityCenterDeformationVector.cs

## Transformation propagation
Propagates deformations of centers in a given frame to the rest of the sequence.
###Implementation
 - **KabschTransformPropagation.cs**

## Surface deformation
Deforms vertices of triangle mesh according to deformations of centers.
###Implementation
 - BasicSurfaceDeformation.cs
 - **CustomSurfaceDeformation.cs**
 - NearestSurfaceDeformation.cs
 - KNearestSurfaceDeformation.cs

## Looping
Deforms the mesh sequence so that the sequence can be played in a loop.
###Implementation
 - TranslationLooping.cs
 - **KabschLooping.cs**