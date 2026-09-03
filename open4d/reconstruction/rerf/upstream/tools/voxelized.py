import numpy as np


def sample_grid_on_voxel(bounds, n=64):

    maxlength = np.max(bounds[1,:] - bounds[0,:])
    center = (bounds[1,:] + bounds[0,:])/2

    bounds = np.stack([center-maxlength/2,center+maxlength/2],axis = 0)

   
    x = np.linspace(bounds[0][0],bounds[1][0],n+1)
    y = np.linspace(bounds[0][1],bounds[1][1],n+1)
    z = np.linspace(bounds[0][2],bounds[1][2],n+1)

    for i in range(n):
        x[i]= (x[i]+x[i+1])/2
        y[i]= (y[i]+y[i+1])/2
        z[i]= (z[i]+z[i+1])/2
    x = x[:-1]
    y = y[:-1]
    z = z[:-1]

    xv, yv, zv = np.meshgrid(x, y, z, indexing='ij')

    point_coords = np.stack([xv,yv,zv],axis = 3)
    
    return point_coords,maxlength