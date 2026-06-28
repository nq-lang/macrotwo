# osgEarth Data

When osgEarth is installed, place a `world.earth` file here.

## Minimal world.earth (copy-paste)

```xml
<map name="World" type="geocentric">
  <image name="readymap" driver="tms">
    <url>http://readymap.org/readymap/tiles/1.0.0/7/</url>
  </image>
</map>
```

## Alternative free imagery sources

- **Stamen Terrain**: https://stamen.com/
- **OpenStreetMap TMS**: tile.openstreetmap.org
- **NASA GIBS**: https://gibs.earthdata.nasa.gov/wmts/

## Without osgEarth

The terminal runs fully without osgEarth using the built-in stub globe renderer.
The stub shows the correct §2 muted colour palette, GMSI heat overlay,
graticule lines, and pulse-ring hotspot markers on a simulated sphere.
