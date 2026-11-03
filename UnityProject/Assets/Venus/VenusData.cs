using System;
using System.IO;
using System.IO.Compression;
using UnityEngine;
namespace VenusWorld {
public readonly struct DVector {
 public readonly double x,y,z;
 public DVector(double x,double y,double z){this.x=x;this.y=y;this.z=z;}
 public double Magnitude=>Math.Sqrt(x*x+y*y+z*z);
 public DVector Normalized=>Magnitude>1e-12?this*(1/Magnitude):new DVector(0,1,0);
 public static DVector operator +(DVector a,DVector b)=>new(a.x+b.x,a.y+b.y,a.z+b.z);
 public static DVector operator -(DVector a,DVector b)=>new(a.x-b.x,a.y-b.y,a.z-b.z);
 public static DVector operator *(DVector a,double s)=>new(a.x*s,a.y*s,a.z*s);
 public static double Dot(DVector a,DVector b)=>a.x*b.x+a.y*b.y+a.z*b.z;
 public Vector3 Float=>new((float)x,(float)y,(float)z);
}
public static class VenusData {
 public const double MeanRadius=6051800,DatumRadius=6051000;public const float Gravity=8.87f;
 static short[] data;public static int Width{get;private set;}public static int Height{get;private set;}
 public static void Load(){if(data!=null)return;var asset=Resources.Load<TextAsset>("VenusDem");if(!asset)throw new FileNotFoundException("Magellan DEM is missing");using var stream=new GZipStream(new MemoryStream(asset.bytes),CompressionMode.Decompress);using var r=new BinaryReader(stream);Width=r.ReadInt32();Height=r.ReadInt32();if(Width!=8192||Height!=4096||r.ReadInt32()!=-32768)throw new InvalidDataException("Unexpected DEM header");byte[] raw=r.ReadBytes(Width*Height*2);if(raw.Length!=Width*Height*2)throw new InvalidDataException("Truncated DEM");data=new short[Width*Height];Buffer.BlockCopy(raw,0,data,0,raw.Length);}
 public static double Elevation(double latitude,double longitude,out bool measured){
  Load();double lon=((longitude+180)%360+360)%360;double fx=lon/360*Width-.5,fy=Math.Max(0,Math.Min(Height-1,(90-latitude)/180*Height-.5));int x=(int)Math.Floor(fx),y=(int)Math.Floor(fy);double tx=fx-x,ty=fy-y,sum=0,weight=0;
  for(int j=0;j<2;j++)for(int i=0;i<2;i++){int xx=((x+i)%Width+Width)%Width,yy=Math.Min(Height-1,y+j);short v=data[yy*Width+xx];if(v==-32768)continue;double w=(i==0?1-tx:tx)*(j==0?1-ty:ty);sum+=w*v;weight+=w;}
  measured=weight>.00001;return measured?sum/weight:0;
 }
 public static DVector Direction(double lat,double lon){lat*=Math.PI/180;lon*=Math.PI/180;return new(Math.Cos(lat)*Math.Cos(lon),Math.Sin(lat),Math.Cos(lat)*Math.Sin(lon));}
 public static void Coordinates(DVector point,out double lat,out double lon){var d=point.Normalized;lat=Math.Asin(d.y)*180/Math.PI;lon=Math.Atan2(d.z,d.x)*180/Math.PI;}
}
public sealed class VenusSite {
 public readonly double latitude,longitude,elevation;public readonly DVector Up,East,North,Origin;
 public bool Measured {get;}
 public VenusSite(double latitude,double longitude){this.latitude=latitude;this.longitude=longitude;elevation=VenusData.Elevation(latitude,longitude,out bool valid);Measured=valid;Up=VenusData.Direction(latitude,longitude);double lon=longitude*Math.PI/180;East=new(-Math.Sin(lon),0,Math.Cos(lon));North=new(-Math.Sin(latitude*Math.PI/180)*Math.Cos(lon),Math.Cos(latitude*Math.PI/180),-Math.Sin(latitude*Math.PI/180)*Math.Sin(lon));Origin=Up*(VenusData.DatumRadius+elevation);}
 public DVector World(Vector3 local)=>Origin+East*local.x+Up*local.y+North*local.z;
 public float Height(float x,float z){
  var point=World(new Vector3(x,0,z));VenusData.Coordinates(point,out double lat,out double lon);double height=VenusData.Elevation(lat,lon,out _);double curvature=-(x*(double)x+z*(double)z)/(2*(VenusData.DatumRadius+elevation));
  // Unresolved metre-scale lava plates: procedural reconstruction, not Magellan measurements.
  float broad=Mathf.PerlinNoise(x*.014f+31,z*.014f+73)*1.4f;
  float plates=Mathf.PerlinNoise(x*.22f+9,z*.22f+11)*.32f;
  float ridges=Mathf.Abs(Mathf.Sin(x*.16f+Mathf.PerlinNoise(x*.04f,z*.04f)*5))*.16f;
  return (float)(height-elevation+curvature)+broad+plates+ridges;
 }
}
}
