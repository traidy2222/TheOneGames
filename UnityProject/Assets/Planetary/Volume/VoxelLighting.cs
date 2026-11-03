using System;
using System.Collections.Generic;
using UnityEngine;
using VenusWorld;
namespace Planetary {
// Diffuse, one-bounce transport against an immutable density field. No depth-based ambient term.
public static class VoxelLighting {
 static DVector From(Vector3 v)=>new(v.x,v.y,v.z);
 public static Color Evaluate(DensityReadView field,DVector point,Vector3 normal,Vector3 sun,Color sky,double distance){
  var n=normal.normalized;var origin=point+From(n)*.3;var sunlight=From(sun.normalized);float direct=field.Sky(origin,sunlight,distance,out _)?1:0;
  Vector3 tangent=Vector3.Cross(n,Mathf.Abs(n.y)<.9f?Vector3.up:Vector3.right).normalized,bitangent=Vector3.Cross(n,tangent);Vector3 irradiance=Vector3.zero;
  const int samples=8;for(int i=0;i<samples;i++){double a=i*2.399963229728653,r=Math.Sqrt((i+.5)/samples);var dir=tangent*(float)(Math.Cos(a)*r)+bitangent*(float)(Math.Sin(a)*r)+n*(float)Math.Sqrt(1-r*r);if(field.Sky(origin,From(dir),distance,out var hit)){irradiance+=new Vector3(sky.r,sky.g,sky.b);}else if((hit-origin).Magnitude<32){var hn=field.Normal(hit);var hp=hit+hn*.25;float cosine=(float)Math.Max(0,DVector.Dot(hn,sunlight));if(cosine>0&&field.Sky(hp,sunlight,distance,out _))irradiance+=new Vector3(.28f,.25f,.20f)*(cosine*.8f);}}
  // Importance sample the small visible sky aperture above a deep, narrow shaft.
  var up=point.Normalized;if(field.Sky(origin,up,distance,out _)){float weight=(float)Math.Max(0,DVector.Dot(From(n),up))*.004f;irradiance+=new Vector3(sky.r,sky.g,sky.b)*(weight*samples);}
  irradiance/=samples;return new Color(irradiance.x,irradiance.y,irradiance.z,direct);
 }
 public static Color[] Bake(VolumeKey key,VolumeMesher.Geometry geometry,DensityReadView field,Vector3 sun,Color sky,double distance){var result=new Color[geometry.vertices.Length];var cache=new Dictionary<Vector3Int,Color>();for(int i=0;i<result.Length;i++){var p=geometry.vertices[i];var cell=new Vector3Int(Mathf.FloorToInt(p.x/4),Mathf.FloorToInt(p.y/4),Mathf.FloorToInt(p.z/4));if(!cache.TryGetValue(cell,out var value)){value=Evaluate(field,key.Origin+From(p),geometry.normals[i],sun,sky,distance);cache[cell]=value;}result[i]=value;}return result;}
 public static Color LampBounce(DensityReadView field,DVector probe,DVector lamp,Vector3 forward,float intensity){if(field.Sample(probe)<.2||intensity<=0)return Color.black;double energy=0;const int rays=20;for(int i=0;i<rays;i++){double y=1-2*(i+.5)/rays,a=i*2.3999632297,r=Math.Sqrt(1-y*y);var direction=new DVector(r*Math.Cos(a),y,r*Math.Sin(a));if(!field.Hit(probe,direction,20,out var hit))continue;var normal=field.Normal(hit);var delta=lamp-hit;double distance=delta.Magnitude;var toLamp=delta.Normalized;double cone=DVector.Dot(toLamp*(-1),From(forward));if(cone<.82)continue;double lambert=Math.Max(0,DVector.Dot(normal,toLamp));if(lambert<=0||!field.Clear(hit+normal*.25,lamp))continue;double falloff=Math.Pow(Math.Clamp((cone-.82)/.13,0,1),2)/(1+distance*distance*.1);energy+=intensity*lambert*falloff*.22;}
  float light=(float)(energy/rays);return new Color(light,light*.92f,light*.80f,1);
 }
}
}
