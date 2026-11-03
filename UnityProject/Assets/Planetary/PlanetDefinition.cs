using System;
using UnityEngine;
using VenusWorld;
namespace Planetary {
[CreateAssetMenu(menuName="Planetary/Planet definition")]
public sealed class PlanetDefinition:ScriptableObject {
 public string displayName="Rocky planet";
 public double radiusMetres=6051800;
 public float surfaceGravity=8.87f;
 public bool useMagellan=false;
 public double elevationDatum=6051000;
 public double atmosphereHeight=100000,cloudHeight=65000;
 public Color hazeColor=new(.48f,.33f,.16f),ambientDaylight=new(.56f,.43f,.28f);
 public double groundFogDensity=.0013,atmosphereScaleHeight=8000,cloudBandWidth=3000;
 public Color cloudHazeColor=new(.70f,.62f,.45f);
 public Material terrainMaterial,cloudMaterial;
 public float proceduralRelief=800,detailAmplitude=.25f;
 public float excavationViewDistance=4096;public int excavationMeshBudget=2048;
 public int seed=173;
 public PlanetSampler CreateSampler(){if(useMagellan)VenusData.Load();return new PlanetSampler(radiusMetres,elevationDatum,useMagellan,proceduralRelief,detailAmplitude,seed);}
}
// Immutable sampler: worker threads never touch Unity objects or the AssetDatabase.
public sealed class PlanetSampler {
 public readonly double Radius,Datum;readonly bool magellan;readonly double relief,detail,seed;
 public PlanetSampler(double radius,double datum,bool magellan,double relief,double detail,int seed){if(radius<100||!double.IsFinite(radius))throw new ArgumentException("Invalid planet radius");Radius=radius;Datum=datum;this.magellan=magellan;this.relief=relief;this.detail=detail;this.seed=seed;}
 public double ReliefBound=>magellan?50000:Math.Abs(relief)*.56+Math.Abs(detail)*1.3+100;
 public double SurfaceRadius(DVector d){d=d.Normalized;double h;if(magellan){VenusData.Coordinates(d,out double lat,out double lon);h=Datum+VenusData.Elevation(lat,lon,out _);}else h=Radius+relief*(Math.Sin(d.x*11+seed)*Math.Cos(d.z*9)+Math.Sin(d.y*17)*.4)*.4;
  var p=d*h;return h+detail*(Math.Sin(p.x*.24+Math.Sin(p.z*.13))*Math.Cos(p.y*.21)+Math.Sin(p.z*.51+p.x*.32)*.3);
 }
 public double Altitude(DVector p)=>p.Magnitude-SurfaceRadius(p);
}
}
