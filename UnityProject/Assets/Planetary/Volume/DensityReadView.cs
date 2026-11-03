using System;
using System.Collections.Generic;
using VenusWorld;
namespace Planetary {
// Immutable edit snapshot: light rays do not contend with interactive density queries.
public sealed class DensityReadView {
 readonly PlanetSampler sampler;readonly Dictionary<VolumeKey,Dictionary<int,float>> edits;
 public DensityReadView(PlanetSampler sampler,Dictionary<VolumeKey,Dictionary<int,float>> edits){this.sampler=sampler;this.edits=edits;}
 public double Base(DVector p)=>p.Magnitude<sampler.Radius-sampler.ReliefBound?p.Magnitude-sampler.Radius:sampler.Altitude(p);
 float At(long x,long y,long z){var key=new VolumeKey(VolumeKey.FloorDiv(x),VolumeKey.FloorDiv(y),VolumeKey.FloorDiv(z));int index=(int)((z-key.z*16)*256+(y-key.y*16)*16+x-key.x*16);return edits.TryGetValue(key,out var page)&&page.TryGetValue(index,out float value)?value:(float)Math.Clamp(Base(new DVector(x,y,z)),-32,32);}
 public double Sample(DVector p){long x=(long)Math.Floor(p.x),y=(long)Math.Floor(p.y),z=(long)Math.Floor(p.z);double a=p.x-x,b=p.y-y,c=p.z-z,result=0;for(int k=0;k<2;k++)for(int j=0;j<2;j++)for(int i=0;i<2;i++)result+=At(x+i,y+j,z+k)*(i==0?1-a:a)*(j==0?1-b:b)*(k==0?1-c:c);return result;}
 public DVector Normal(DVector p){const double e=.3;return new DVector(Sample(p+new DVector(e,0,0))-Sample(p-new DVector(e,0,0)),Sample(p+new DVector(0,e,0))-Sample(p-new DVector(0,e,0)),Sample(p+new DVector(0,0,e))-Sample(p-new DVector(0,0,e))).Normalized;}
 // Returns true only when a ray reaches unobstructed exterior. A distance/iteration limit is never counted as sky.
 public bool Sky(DVector origin,DVector direction,double maximum,out DVector hit){direction=direction.Normalized;double t=.12;for(int i=0;i<8192&&t<maximum;i++){var p=origin+direction*t;double value=Sample(p);if(value<.025){hit=p;return false;}if(Base(p)>4&&DVector.Dot(p.Normalized,direction)>.02){hit=p;return true;}t+=Math.Clamp(value*.65,.08,16);}hit=origin+direction*Math.Min(t,maximum);return false;}
 public bool Clear(DVector a,DVector b){var delta=b-a;double length=delta.Magnitude;if(length<.2)return true;var d=delta*(1/length);double t=.12;for(int i=0;i<8192&&t<length-.12;i++){double value=Sample(a+d*t);if(value<.025)return false;t+=Math.Clamp(value*.6,.06,16);}return t>=length-.12;}
 public bool Hit(DVector origin,DVector direction,double maximum,out DVector hit){direction=direction.Normalized;double t=.15;for(int i=0;i<1024&&t<maximum;i++){var p=origin+direction*t;double value=Sample(p);if(value<.025){hit=p;return true;}t+=Math.Clamp(value*.6,.06,8);}hit=origin+direction*maximum;return false;}
}
}
