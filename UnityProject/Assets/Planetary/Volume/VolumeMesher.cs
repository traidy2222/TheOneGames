using System;
using System.Collections.Generic;
using UnityEngine;
namespace Planetary {
// Pure geometry worker. One-cell halo gives identical gradients on shared chunk faces.
public static class VolumeMesher {
 public sealed class Geometry {public Vector3[] vertices,normals,metric;public int[] triangles;}
 static readonly Vector3Int[] corners={new(0,0,0),new(1,0,0),new(1,1,0),new(0,1,0),new(0,0,1),new(1,0,1),new(1,1,1),new(0,1,1)};
 static readonly int[,] tets={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
 public static Geometry Build(VolumeKey key,float[] field){
  var vertices=new List<Vector3>();var normals=new List<Vector3>();var metric=new List<Vector3>();var triangles=new List<int>();var points=new Vector3[8];var gradients=new Vector3[8];var density=new float[8];var inside=new int[4];var outside=new int[4];Vector3 outward=Vector3.up;var anchor=key.Origin;var offset=new Vector3((float)(anchor.x%2048),(float)(anchor.y%2048),(float)(anchor.z%2048));
  float At(int x,int y,int z)=>field[((z+1)*19+y+1)*19+x+1];
  for(int z=0;z<16;z++)for(int y=0;y<16;y++)for(int x=0;x<16;x++){
   int count=0;for(int i=0;i<8;i++){var p=corners[i]+new Vector3Int(x,y,z);points[i]=p;density[i]=At(p.x,p.y,p.z);if(density[i]<0)count++;}if(count==0||count==8)continue;
   for(int i=0;i<8;i++){var p=points[i];int a=(int)p.x,b=(int)p.y,c=(int)p.z;gradients[i]=new Vector3(At(a+1,b,c)-At(a-1,b,c),At(a,b+1,c)-At(a,b-1,c),At(a,b,c+1)-At(a,b,c-1));}
   for(int t=0;t<6;t++){int a=0,b=0;for(int j=0;j<4;j++){int i=tets[t,j];if(density[i]<0)inside[a++]=i;else outside[b++]=i;}if(a==0||a==4)continue;Vector3 solid=Vector3.zero,air=Vector3.zero;for(int i=0;i<a;i++)solid+=points[inside[i]];for(int i=0;i<b;i++)air+=points[outside[i]];outward=air/b-solid/a;
    if(a==1)Tri(Edge(inside[0],outside[0]),Edge(inside[0],outside[1]),Edge(inside[0],outside[2]));else if(a==3)Tri(Edge(outside[0],inside[0]),Edge(outside[0],inside[1]),Edge(outside[0],inside[2]));else{var p=Edge(inside[0],outside[0]);var q=Edge(inside[0],outside[1]);var r=Edge(inside[1],outside[0]);var s=Edge(inside[1],outside[1]);Tri(p,q,r);Tri(q,s,r);}
   }
  }
  return new Geometry{vertices=vertices.ToArray(),normals=normals.ToArray(),metric=metric.ToArray(),triangles=triangles.ToArray()};
  (Vector3 p,Vector3 n) Edge(int a,int b){float t=density[a]/(density[a]-density[b]);return(Vector3.Lerp(points[a],points[b],t),Vector3.Lerp(gradients[a],gradients[b],t).normalized);}
  void Tri((Vector3 p,Vector3 n)a,(Vector3 p,Vector3 n)b,(Vector3 p,Vector3 n)c){var cross=Vector3.Cross(b.p-a.p,c.p-a.p);if(cross.sqrMagnitude<1e-12)return;if(Vector3.Dot(cross,outward)<0)(b,c)=(c,b);int i=vertices.Count;vertices.Add(a.p);vertices.Add(b.p);vertices.Add(c.p);normals.Add(a.n);normals.Add(b.n);normals.Add(c.n);metric.Add(a.p+offset);metric.Add(b.p+offset);metric.Add(c.p+offset);triangles.Add(i);triangles.Add(i+1);triangles.Add(i+2);}
 }
}
}
