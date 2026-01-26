#ifndef RI_CALLBACKS_H
#define RI_CALLBACKS_H

#include "ri.h"

// Callback interface that allows the RIB parser to call either
// the rendering library (libri) or the RIB output library (librib)

typedef struct RiCallbacks {
    // Relationship to external world
    void (*Begin)(RtToken name);
    void (*End)(void);

    // Variable declarations
    void (*Declare)(const char* name, const char* declaration);

    // Options
    void (*Format)(RtInt xres, RtInt yres, RtFloat aspect);
    void (*Display)(RtToken name, RtToken type, RtToken mode);
    void (*Projection)(RtToken name, RtToken *tokens, RtPointer *values, int count);
    void (*PixelSamples)(RtFloat xsamples, RtFloat ysamples);
    void (*PixelFilter)(RtToken filtername, RtFloat xwidth, RtFloat ywidth);
    void (*DepthOfField)(RtFloat fstop, RtFloat focallength, RtFloat focaldistance);
    void (*Shutter)(RtFloat open, RtFloat close);
    void (*ShadingRate)(RtFloat size);
    void (*Option)(RtToken name, RtToken *tokens, RtPointer *values, int count);
    void (*Hider)(RtToken type, RtToken *tokens, RtPointer *values, int count);

    // Graphics State
    void (*AttributeBegin)(void);
    void (*AttributeEnd)(void);
    void (*TransformBegin)(void);
    void (*TransformEnd)(void);

    // Motion blur
    void (*MotionBegin)(RtInt n, RtFloat* times);
    void (*MotionEnd)(void);

    // Transformations
    void (*Identity)(void);
    void (*Transform)(RtMatrix transform);
    void (*ConcatTransform)(RtMatrix transform);
    void (*Translate)(RtFloat dx, RtFloat dy, RtFloat dz);
    void (*Rotate)(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz);
    void (*Scale)(RtFloat sx, RtFloat sy, RtFloat sz);

    // Basis
    void (*Basis)(RtToken ubasis, RtInt ustep, RtToken vbasis, RtInt vstep);

    // Attributes
    void (*Color)(RtColor color);
    void (*Opacity)(RtColor opacity);
    void (*Surface)(RtToken name, RtToken *tokens, RtPointer *values, int count);
    void (*Orientation)(RtToken orientation);
    void (*ReverseOrientation)(void);
    void (*Sides)(RtInt nsides);

    // Scene Structure
    void (*WorldBegin)(void);
    void (*WorldEnd)(void);

    // Primitives
    void (*Sphere)(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                   RtToken *tokens, RtPointer *values, int count);
    void (*Cylinder)(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                     RtToken *tokens, RtPointer *values, int count);
    void (*Cone)(RtFloat height, RtFloat radius, RtFloat tmax,
                 RtToken *tokens, RtPointer *values, int count);
    void (*Disk)(RtFloat height, RtFloat radius, RtFloat tmax,
                 RtToken *tokens, RtPointer *values, int count);
    void (*Torus)(RtFloat majorrad, RtFloat minorrad, RtFloat phimin,
                  RtFloat phimax, RtFloat tmax,
                  RtToken *tokens, RtPointer *values, int count);
    void (*Paraboloid)(RtFloat rmax, RtFloat zmin, RtFloat zmax, RtFloat tmax,
                       RtToken *tokens, RtPointer *values, int count);
    void (*Hyperboloid)(RtPoint point1, RtPoint point2, RtFloat tmax,
                        RtToken *tokens, RtPointer *values, int count);
    void (*Polygon)(RtInt nvertices, RtToken *tokens, RtPointer *values, int count);
    void (*Patch)(RtToken type, RtToken *tokens, RtPointer *values, int count);
    void (*Geometry)(RtToken type, RtToken *tokens, RtPointer *values, int count);

    // Lighting
    void (*LightSource)(RtToken name, RtToken *tokens, RtPointer *values, int count);
    void (*Illuminate)(RtToken light, RtBoolean onoff);

    // Retained Geometry
    void (*ObjectBegin)(void);
    void (*ObjectEnd)(void);
    void (*ObjectInstance)(RtInt handle);

} RiCallbacks;

// Pre-defined callback tables (defined in their respective libraries)
extern RiCallbacks ri_render_callbacks;   // libri - actually renders
extern RiCallbacks ri_output_callbacks;   // librib - writes RIB

#endif // RI_CALLBACKS_H
