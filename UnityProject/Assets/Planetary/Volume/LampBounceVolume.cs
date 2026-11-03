using System;
using System.Threading.Tasks;
using UnityEngine;
using VenusWorld;
namespace Planetary {
// Local dynamic one-bounce probe volume for the moving lamp. The mesh stores sky/sun transport separately.
public sealed class LampBounceVolume:IDisposable {
 const int Side=7;const double Spacing=3;
 sealed class Result {public DVector origin;public Color[] pixels;}
 Task<Result> task;Texture3D texture;DVector origin;double next;bool ready,disposed;
 public bool Ready=>ready;public bool Pending=>task!=null;
 public LampBounceVolume(){texture=new Texture3D(Side,Side,Side,TextureFormat.RGBAHalf,false){filterMode=FilterMode.Trilinear,wrapMode=TextureWrapMode.Clamp};texture.SetPixels(new Color[Side*Side*Side]);texture.Apply();Shader.SetGlobalTexture("_LampBounce",texture);Shader.SetGlobalFloat("_BounceReady",0);}
 public void Tick(DensityReadView field,DVector observer,Vector3 forward,bool lampEnabled,DVector lampPosition){if(disposed)return;if(task!=null&&task.IsCompleted){if(task.IsFaulted)throw task.Exception;var result=task.Result;task=null;origin=result.origin;texture.SetPixels(result.pixels);texture.Apply();ready=true;}
  Shader.SetGlobalVector("_BounceMin",(origin-observer).Float);Shader.SetGlobalVector("_BounceSize",Vector3.one*(float)((Side-1)*Spacing));Shader.SetGlobalFloat("_BounceReady",ready&&lampEnabled?1:0);
  if(field==null||!lampEnabled||task!=null||Time.realtimeSinceStartupAsDouble<next)return;next=Time.realtimeSinceStartupAsDouble+.4;var start=new DVector(Math.Floor(observer.x/Spacing)*Spacing-9,Math.Floor(observer.y/Spacing)*Spacing-9,Math.Floor(observer.z/Spacing)*Spacing-9);var lamp=lampPosition;task=Task.Run(()=>{var pixels=new Color[Side*Side*Side];for(int z=0;z<Side;z++)for(int y=0;y<Side;y++)for(int x=0;x<Side;x++){var p=start+new DVector(x*Spacing,y*Spacing,z*Spacing);pixels[(z*Side+y)*Side+x]=VoxelLighting.LampBounce(field,p,lamp,forward,5);}return new Result{origin=start,pixels=pixels};});
 }
 public void Dispose(){if(disposed)return;disposed=true;Shader.SetGlobalFloat("_BounceReady",0);if(Application.isPlaying)UnityEngine.Object.Destroy(texture);else UnityEngine.Object.DestroyImmediate(texture);}
}
}
