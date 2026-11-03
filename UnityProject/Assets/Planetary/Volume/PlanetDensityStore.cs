using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using VenusWorld;
namespace Planetary {
// A lattice sample has exactly one owner, including on negative axes and chunk boundaries.
public readonly struct VolumeKey:IEquatable<VolumeKey> {
 public const int Size=16;
 public readonly long x,y,z;
 public VolumeKey(long x,long y,long z){this.x=x;this.y=y;this.z=z;}
 public static long FloorDiv(long n)=>n>=0?n/Size:(n-(Size-1))/Size;
 public static VolumeKey At(DVector p)=>new(FloorDiv((long)Math.Floor(p.x)),FloorDiv((long)Math.Floor(p.y)),FloorDiv((long)Math.Floor(p.z)));
 public DVector Origin=>new(x*Size,y*Size,z*Size);
 public bool Equals(VolumeKey k)=>x==k.x&&y==k.y&&z==k.z;
 public override bool Equals(object o)=>o is VolumeKey k&&Equals(k);
 public override int GetHashCode()=>HashCode.Combine(x,y,z);
 public override string ToString()=>$"{x}_{y}_{z}";
}
public sealed class PlanetDensityStore {
 sealed class Page {public readonly Dictionary<int,float> values=new();public bool dirty;public long used;}
 readonly object gate=new();readonly PlanetSampler sampler;readonly string folder;
 readonly Dictionary<VolumeKey,Page> pages=new();readonly HashSet<VolumeKey> modified=new();
 readonly List<List<Change>> history=new();long clock;const int CacheLimit=128;
 readonly struct Change {public readonly long x,y,z;public readonly float before;public Change(long x,long y,long z,float value){this.x=x;this.y=y;this.z=z;before=value;}}
 public event Action<DVector,DVector> Changed;
 public int CachedPages{get{lock(gate)return pages.Count;}}
 public int EditCount{get;private set;}
 public PlanetDensityStore(PlanetSampler sampler,string folder){this.sampler=sampler;this.folder=folder;Directory.CreateDirectory(folder);foreach(string path in Directory.EnumerateFiles(folder,"*.vxl")){var parts=Path.GetFileNameWithoutExtension(path).Split('_');if(parts.Length==3&&long.TryParse(parts[0],out long x)&&long.TryParse(parts[1],out long y)&&long.TryParse(parts[2],out long z))modified.Add(new VolumeKey(x,y,z));}}
 public double BaseDensity(DVector p){double r=p.Magnitude;double conservativeRelief=sampler.ReliefBound;if(r<sampler.Radius-conservativeRelief)return r-sampler.Radius;return r-sampler.SurfaceRadius(p);}
 Page Get(VolumeKey key){if(pages.TryGetValue(key,out var page)){page.used=++clock;return page;}if(pages.Count>=CacheLimit){VolumeKey oldest=default;long age=long.MaxValue;foreach(var pair in pages)if(pair.Value.used<age){age=pair.Value.used;oldest=pair.Key;}Write(oldest,pages[oldest]);pages.Remove(oldest);}page=new Page{used=++clock};string path=Path.Combine(folder,key+".vxl");if(File.Exists(path)){using var stream=new GZipStream(File.OpenRead(path),CompressionMode.Decompress);using var reader=new BinaryReader(stream);if(reader.ReadInt32()!=0x50564c31||reader.ReadInt64()!=key.x||reader.ReadInt64()!=key.y||reader.ReadInt64()!=key.z)throw new InvalidDataException("Voxel chunk identity mismatch");int count=reader.ReadInt32();if(count<0||count>4096)throw new InvalidDataException("Invalid voxel count");for(int i=0;i<count;i++){int index=reader.ReadInt32();float value=reader.ReadSingle();if(index<0||index>=4096||!float.IsFinite(value)||page.values.ContainsKey(index))throw new InvalidDataException("Invalid voxel sample");page.values.Add(index,value);}}pages.Add(key,page);return page;}
 static int Index(long x,long y,long z,VolumeKey key)=>(int)((z-key.z*16)*256+(y-key.y*16)*16+x-key.x*16);
 float Read(long x,long y,long z){var key=new VolumeKey(VolumeKey.FloorDiv(x),VolumeKey.FloorDiv(y),VolumeKey.FloorDiv(z));var page=Get(key);return page.values.TryGetValue(Index(x,y,z,key),out float value)?value:(float)Math.Clamp(BaseDensity(new DVector(x,y,z)),-32,32);}
 void Put(long x,long y,long z,float value){var key=new VolumeKey(VolumeKey.FloorDiv(x),VolumeKey.FloorDiv(y),VolumeKey.FloorDiv(z));var page=Get(key);page.values[Index(x,y,z,key)]=value;page.dirty=true;modified.Add(key);}
 public double Sample(DVector p){lock(gate){long x=(long)Math.Floor(p.x),y=(long)Math.Floor(p.y),z=(long)Math.Floor(p.z);double a=p.x-x,b=p.y-y,c=p.z-z,result=0;for(int k=0;k<2;k++)for(int j=0;j<2;j++)for(int i=0;i<2;i++)result+=Read(x+i,y+j,z+k)*(i==0?1-a:a)*(j==0?1-b:b)*(k==0?1-c:c);return result;}}
 public float[] Snapshot(VolumeKey key){lock(gate){var values=new float[19*19*19];var o=key.Origin;for(int z=-1;z<=17;z++)for(int y=-1;y<=17;y++)for(int x=-1;x<=17;x++)values[((z+1)*19+y+1)*19+x+1]=Read((long)o.x+x,(long)o.y+y,(long)o.z+z);return values;}}
 public int Sculpt(DVector center,double radius,bool fill){if(!double.IsFinite(center.x)||!double.IsFinite(center.y)||!double.IsFinite(center.z)||!double.IsFinite(radius)||radius<1||radius>8)throw new ArgumentException("Invalid excavation brush");var changes=new List<Change>();
  lock(gate){for(long z=(long)Math.Floor(center.z-radius-1);z<=Math.Ceiling(center.z+radius+1);z++)for(long y=(long)Math.Floor(center.y-radius-1);y<=Math.Ceiling(center.y+radius+1);y++)for(long x=(long)Math.Floor(center.x-radius-1);x<=Math.Ceiling(center.x+radius+1);x++){double sphere=(new DVector(x,y,z)-center).Magnitude-radius;if(sphere>1)continue;float before=Read(x,y,z),after=fill?Math.Min(before,(float)sphere):Math.Max(before,(float)-sphere);if(Math.Abs(after-before)<1e-6)continue;changes.Add(new Change(x,y,z,before));Put(x,y,z,after);}if(changes.Count>0){history.Add(changes);if(history.Count>8)history.RemoveAt(0);EditCount++;}}
  if(changes.Count>0){var extent=new DVector(radius+2,radius+2,radius+2);Changed?.Invoke(center-extent,center+extent);}return changes.Count;
 }
 public bool Undo(){DVector lo,hi;lock(gate){if(history.Count==0)return false;var changes=history[^1];history.RemoveAt(history.Count-1);lo=new DVector(double.MaxValue,double.MaxValue,double.MaxValue);hi=new DVector(double.MinValue,double.MinValue,double.MinValue);foreach(var c in changes){Put(c.x,c.y,c.z,c.before);lo=new DVector(Math.Min(lo.x,c.x-2),Math.Min(lo.y,c.y-2),Math.Min(lo.z,c.z-2));hi=new DVector(Math.Max(hi.x,c.x+2),Math.Max(hi.y,c.y+2),Math.Max(hi.z,c.z+2));}EditCount++;}Changed?.Invoke(lo,hi);return true;}
 void Write(VolumeKey key,Page page){if(!page.dirty)return;string path=Path.Combine(folder,key+".vxl"),temp=path+".tmp";using(var stream=new GZipStream(File.Create(temp),CompressionLevel.Fastest))using(var writer=new BinaryWriter(stream)){writer.Write(0x50564c31);writer.Write(key.x);writer.Write(key.y);writer.Write(key.z);writer.Write(page.values.Count);foreach(var pair in page.values){writer.Write(pair.Key);writer.Write(pair.Value);}}if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);page.dirty=false;}
 public VolumeKey[] EditedKeys(DVector observer,double distance){lock(gate){var result=new List<VolumeKey>();double limit=distance+28;foreach(var key in modified)if((key.Origin+new DVector(8,8,8)-observer).Magnitude<=limit)result.Add(key);return result.ToArray();}}
 public DensityReadView ReadView(DVector observer,double distance){var copy=new Dictionary<VolumeKey,Dictionary<int,float>>();var disk=new List<VolumeKey>();lock(gate){foreach(var key in modified)if((key.Origin+new DVector(8,8,8)-observer).Magnitude<distance+32){if(pages.TryGetValue(key,out var page))copy[key]=new Dictionary<int,float>(page.values);else disk.Add(key);}}foreach(var key in disk){string path=Path.Combine(folder,key+".vxl");var values=new Dictionary<int,float>();using var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete);using var stream=new GZipStream(file,CompressionMode.Decompress);using var reader=new BinaryReader(stream);if(reader.ReadInt32()!=0x50564c31||reader.ReadInt64()!=key.x||reader.ReadInt64()!=key.y||reader.ReadInt64()!=key.z)throw new InvalidDataException("Lighting snapshot identity mismatch");int count=reader.ReadInt32();if(count<0||count>4096)throw new InvalidDataException("Invalid lighting snapshot count");for(int i=0;i<count;i++){int index=reader.ReadInt32();float value=reader.ReadSingle();if(index<0||index>=4096||!float.IsFinite(value))throw new InvalidDataException("Invalid lighting snapshot sample");values.Add(index,value);}copy[key]=values;}return new DensityReadView(sampler,copy);}

 public void Save(){lock(gate)foreach(var pair in pages)Write(pair.Key,pair.Value);}
 public void Reload(){lock(gate){pages.Clear();history.Clear();}Changed?.Invoke(new DVector(-double.MaxValue,-double.MaxValue,-double.MaxValue),new DVector(double.MaxValue,double.MaxValue,double.MaxValue));}
}
}
