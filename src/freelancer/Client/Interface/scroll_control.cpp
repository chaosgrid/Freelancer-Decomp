// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/scroll_control.h"

#include "Core/game_options.h"      // GetGameOption (FUN_005792b0)
#include "Core/globals.h"           // GetClientElapsedTime (FUN_0042d660)
#include "Space/HUD/hud_element_list.h"  // HudVec3ScaleByInverse (FUN_004c8b10)
#include "Space/HUD/hud_target.h"        // HudTransformPoint (FUN_004e7090)
#include "Common/input_manager.h"     // GetClientSoftwareCursor (DAT_00616840 / DAT_00616844)
#include "NeuroNet/nn_ui_pulse.h"       // G_NnPaletteCyan
#include "Space/Cameras/space_camera.h"      // AsCamera, FSpaceCamera
#include "Space/star_sphere.h"       // Win3DIdentityRotation3x3 (FUN_004215b0)
#include "Interface/win3d.h"
#include "Interface/win3d_engine_submit.h" // FClientMeshHolder::FClientMeshHolder::SubmitWin3DEngineInstance, Win3DRayHitsAabbHalfExtents
#include "Interface/win3d_immediate.h"   // Win3DDisable (FUN_00421ed0)
#include "Interface/win_factory.h"

#include "common_csys.h"       // Acquire3DMathEngine (FUN_005b73e0)
#include "common_ini.h"
#include "DALib.h"
#include "engine/engbase.h"
#include "x86math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// DAT_0067c488 (static init LAB_0057fd20, atexit LAB_0057fd40).
WINFACTORY<ScrollControl> G_ScrollControlFactory("ScrollControl");

unsigned char G_ScrollControlDragging = 0; // DAT_0067c4c4

namespace
{
    // DAT_0067c4c0 -- this TU's 3DMathEngine, acquired on first use.
    I3DMathEngine* G_ScrollControlMath = nullptr;

    I3DMathEngine* ScrollControlMath()
    {
        if (G_ScrollControlMath == nullptr)
            G_ScrollControlMath = Acquire3DMathEngine();
        return G_ScrollControlMath;
    }

    // The transform every ScrollControl field starts as, and what a "<part>_hp" key resets
    // before asking the parent for the hardpoint.
    void SetIdentityTransform(Transform* Xform)
    {
        Win3DIdentityRotation3x3(&Xform->Rotation);
        Xform->Translation.z = 0.0f;
        Xform->Translation.y = 0.0f;
        Xform->Translation.x = 0.0f;
    }

    // The instance-bound test plus SetInstanceMatrix / UpdateInstance pair every slot inlines.
    void PushInstanceMatrix(FClientMeshHolder* Holder, Transform* Xform)
    {
        if (!Holder->HasInstance())
            return;
        DALib::Engine->SetInstanceMatrix(Holder->EngineInstance, Xform);
        DALib::Engine->UpdateInstance(Holder->EngineInstance, 0, nullptr);
    }

    // The same with SetInstancePosition (the "<part>_offset" keys).
    void PushInstancePosition(FClientMeshHolder* Holder, Vector* Position)
    {
        if (!Holder->HasInstance())
            return;
        DALib::Engine->SetInstancePosition(Holder->EngineInstance, Position);
        DALib::Engine->UpdateInstance(Holder->EngineInstance, 0, nullptr);
    }

    // The "<part>_mesh" sequence.
    void LoadPartMesh(ScrollControl* Control, FClientMeshHolder* Holder, char const* Path,
                      Transform* Xform)
    {
        Holder->ReleaseWithAnim();
        if (Path[0] != '\0')
            Holder->LoadFromPath(Path, 0, false);
        PushInstanceMatrix(Holder, Xform);
        Control->RefreshAttachment();
    }

    // The "<part>_hp" sequence with a non-null Parent.
    void AskHardpoint(Win3D* Parent, char const* Name, Transform* Xform)
    {
        WinControl* const Owner = Parent->AsWinControl();
        if (Owner == nullptr)
            return;
        Owner->GetHardpointTransform(Name, Xform);
    }

    // The three floats of a vector key, read in retail's order (index 1, 0, 2).
    void ReadVector(INI_Reader* Reader, Vector* Out)
    {
        float const Y = Reader->get_value_float(1);
        float const X = Reader->get_value_float(0);
        float const Z = Reader->get_value_float(2);
        Out->x = X;
        Out->y = Y;
        Out->z = Z;
    }

    // A flag key: get_value_float(0) != 0.0.
    unsigned char ReadFlag(INI_Reader* Reader)
    {
        return (static_cast<double>(Reader->get_value_float(0)) != 0.0) ? 1 : 0;
    }

    // FUN_00507e70 (cdecl) -- the bit-trick reciprocal square root with one Newton step; retail
    // leaves the result unrounded on the x87 stack, the port returns double.
    double FastInvSqrt(float X)
    {
        int Bits = 0;
        memcpy(&Bits, &X, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &Bits, sizeof(Y));
        return (1.5 - static_cast<double>(Y) * Y * X * 0.5) * Y;
    }

    // The same step inlined with the squared length kept double (rounded to float for the bits
    // only), times the length, stored as float.
    float FastLengthExtended(double Len2)
    {
        float const Len2Float = static_cast<float>(Len2);
        int Bits = 0;
        memcpy(&Bits, &Len2Float, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Y = 0.0f;
        memcpy(&Y, &Bits, sizeof(Y));
        return static_cast<float>((1.5 - static_cast<double>(Y) * Y * Len2 * 0.5) * Y * Len2);
    }

    // The FastInvSqrt form: the squared length rounded to float first.
    float FastLengthStored(double Len2)
    {
        float const Len2Float = static_cast<float>(Len2);
        return static_cast<float>(FastInvSqrt(Len2Float) * Len2Float);
    }

    // z * z + y * y + x * x in that order (every call site here sums from z).
    double SquaredLengthZyx(Vector const& V)
    {
        return (static_cast<double>(V.z) * V.z + static_cast<double>(V.y) * V.y) +
               static_cast<double>(V.x) * V.x;
    }

    // FUN_00423b60 (cdecl).
    Vector Subtract(Vector const& A, Vector const& B)
    {
        Vector Out;
        Out.x = A.x - B.x;
        Out.y = A.y - B.y;
        Out.z = A.z - B.z;
        return Out;
    }

    // FUN_00423b30 (cdecl).
    Vector Add(Vector const& A, Vector const& B)
    {
        Vector Out;
        Out.x = A.x + B.x;
        Out.y = A.y + B.y;
        Out.z = A.z + B.z;
        return Out;
    }

    // Through the Win3D camera (DAT_0067dbf8); only the low byte of the result is tested.
    bool ProjectToScreen(Vector const& World, Vector* OutScreen)
    {
        ICamera* const Camera = AsCamera(Win3DGetCamera());
        return static_cast<unsigned char>(
                   Camera->Project(&OutScreen->x, &OutScreen->y, &OutScreen->z,
                                   &World)) != 0;
    }

    // FUN_005a1130 (thiscall on the half extents, RET 8).
    bool RayHits(Vector const& HalfExtents, Transform const& Xform, Vector const* Ray)
    {
        return Win3DRayHitsAabbHalfExtents(&HalfExtents, &Xform, Ray);
    }

    // One tinted part draw, inlined in every Draw block (the colour conversion is
    // FUN_004a1af0's arithmetic).
    void DrawPartMesh(FClientMeshHolder* Holder, unsigned long const* Rgba)
    {
        Holder->SetColor(reinterpret_cast<unsigned char const*>(Rgba));
        Holder->SubmitWin3DEngineInstance(Win3DGetCamera(), 0, 1.0f, 0);
        Win3DDisable(kWin3DDepthTest);
    }

    int CursorPixelX()
    {
        int X = 0;
        int Y = 0;
        GetClientSoftwareCursor(&X, &Y, nullptr);
        return X;
    }

    int CursorPixelY()
    {
        int X = 0;
        int Y = 0;
        GetClientSoftwareCursor(&X, &Y, nullptr);
        return Y;
    }
}

ScrollControl::ScrollControl()
{
    // FUN_0057fd50. Retail sets the working transforms to identity a second time; same values,
    // not repeated here.
    ClientMeshHolderConstruct(&Back.Holder);
    SetIdentityTransform(&Back.Xform);
    SetIdentityTransform(&Back.Hp);
    ClientMeshHolderConstruct(&Next.Holder);
    SetIdentityTransform(&Next.Xform);
    SetIdentityTransform(&Next.Hp);
    ClientMeshHolderConstruct(&Prev.Holder);
    SetIdentityTransform(&Prev.Xform);
    SetIdentityTransform(&Prev.Hp);
    ClientMeshHolderConstruct(&ThumbHolder);
    SetIdentityTransform(&ThumbStart);
    SetIdentityTransform(&ThumbEnd);
    SetIdentityTransform(&ThumbXform);
    SetIdentityTransform(&ThumbHp0);
    SetIdentityTransform(&ThumbHp1);

    Back.Offset = {0.0f, 0.0f, 0.0f};
    Back.MouseSize = {0.0f, 0.0f, 0.0f};
    Next.Offset = {0.0f, 0.0f, 0.0f};
    Next.MouseSize = {0.0f, 0.0f, 0.0f};
    Prev.Offset = {0.0f, 0.0f, 0.0f};
    Prev.MouseSize = {0.0f, 0.0f, 0.0f};
    ThumbOffset0 = {0.0f, 0.0f, 0.0f};
    ThumbOffset1 = {0.0f, 0.0f, 0.0f};
    ThumbMouse = {0.0f, 0.0f, 0.0f};

    Back.bNoMeshRender = 0;
    Next.bNoMeshRender = 0;
    Prev.bNoMeshRender = 0;
    bThumbNoMeshRender = 0;

    // RepeatTime (+0x670) and bPageTowardMin (+0x67b) are not written.
    PageSize = 1;
    Step = 1;
    MinRange = 0;
    MaxRange = 0;
    Value = 0;
    bSliderBehavior = 0;
    bNoZEnable = 0;
    bThumbValid = 0;
    bDragging = 0;
    bPaging = 0;
    bNextPressed = 0;
    bPrevPressed = 0;
    bThumbHover = 0;
    bNextHover = 0;
    bPrevHover = 0;
}

ScrollControl::~ScrollControl()
{
    // FUN_00580190. Back, prev, next, thumb, in that order.
    Back.Holder.ReleaseWithAnim();
    Prev.Holder.ReleaseWithAnim();
    Next.Holder.ReleaseWithAnim();
    ThumbHolder.ReleaseWithAnim();

    // A held thumb, back plate or button drops the capture.
    if (bDragging != 0 || bPaging != 0 || bNextPressed != 0 || bPrevPressed != 0)
    {
        Win3DRemoveCapture(this);
        G_ScrollControlDragging = 0;
    }
}

void ScrollControl::UpdateThumb()
{
    // FUN_00580290.
    int const Min = MinRange;
    int const Max = MaxRange;
    if (Min >= Max || Min > Value || Max < Value)
    {
        bThumbValid = 0;
        return;
    }

    // Kept double.
    double const T = static_cast<double>(Value - Min) / static_cast<double>(Max - Min);
    bThumbValid = 1;
    ThumbXform = ThumbStart;

    // dz is rounded to float before its product, dx * t is rounded to float, dy * t stays double.
    double const Dx = static_cast<double>(ThumbEnd.Translation.x) - ThumbStart.Translation.x;
    double const Dy = static_cast<double>(ThumbEnd.Translation.y) - ThumbStart.Translation.y;
    float const Dz = static_cast<float>(static_cast<double>(ThumbEnd.Translation.z) -
                                        ThumbStart.Translation.z);
    float const Dxt = static_cast<float>(Dx * T);
    float const Dzt = static_cast<float>(static_cast<double>(Dz) * T);
    ThumbXform.Translation.x = static_cast<float>(static_cast<double>(Dxt) +
                                                  ThumbStart.Translation.x);
    ThumbXform.Translation.y = static_cast<float>(Dy * T + ThumbStart.Translation.y);
    ThumbXform.Translation.z = static_cast<float>(static_cast<double>(Dzt) +
                                                  ThumbStart.Translation.z);

    PushInstanceMatrix(&ThumbHolder, &ThumbXform);
}

void ScrollControl::UpdateLayout()
{
    // FUN_005803d0. With a WinControl parent the working transforms are rebuilt: each is its
    // hardpoint transform moved by (offset + position) + WinControl::Offset. The thumb's start
    // and end add in a different order, as retail.
    if (Parent != nullptr && Parent->AsWinControl() != nullptr)
    {
        Vector const Position = {Win3D::Position.x, Win3D::Position.y, Win3D::Position.z};
        Vector const ControlOffset = {Offset.x, Offset.y, Offset.z};

        Back.Xform = Back.Hp;
        Vector Moved = Add(Add(Back.Offset, Position), ControlOffset);
        Back.Xform.Translation.x += Moved.x;
        Back.Xform.Translation.y += Moved.y;
        Back.Xform.Translation.z += Moved.z;

        Next.Xform = Next.Hp;
        Moved = Add(Add(Next.Offset, Position), ControlOffset);
        Next.Xform.Translation.x += Moved.x;
        Next.Xform.Translation.y += Moved.y;
        Next.Xform.Translation.z += Moved.z;

        // Retail's second sum for prev goes through FUN_00422b80.
        Prev.Xform = Prev.Hp;
        Moved = Add(Add(Prev.Offset, Position), ControlOffset);
        Prev.Xform.Translation.x += Moved.x;
        Prev.Xform.Translation.y += Moved.y;
        Prev.Xform.Translation.z += Moved.z;

        ThumbStart = ThumbHp0;
        Moved = Add(Add(Position, ThumbOffset0), ControlOffset);
        ThumbStart.Translation.x += Moved.x;
        ThumbStart.Translation.y += Moved.y;
        ThumbStart.Translation.z += Moved.z;

        ThumbEnd = ThumbHp1;
        Moved = Add(Add(ThumbOffset1, Position), ControlOffset);
        ThumbEnd.Translation.x += Moved.x;
        ThumbEnd.Translation.y += Moved.y;
        ThumbEnd.Translation.z += Moved.z;
    }

    PushInstanceMatrix(&Back.Holder, &Back.Xform);
    PushInstanceMatrix(&Next.Holder, &Next.Xform);
    PushInstanceMatrix(&Prev.Holder, &Prev.Xform);
    UpdateThumb();
}

void ScrollControl::ChoosePageDirection(unsigned ScreenX, unsigned ScreenY)
{
    // FUN_00580750 (RET 8). At either end of the range the answer is fixed first.
    if (Value == MinRange)
        bPageTowardMin = 0;
    else if (Value == MaxRange)
        bPageTowardMin = 1;

    // The pixel converts as unsigned.
    Vector const Mouse = {static_cast<float>(ScreenX), static_cast<float>(ScreenY), 0.0f};
    Vector const Zero = {0.0f, 0.0f, 0.0f};

    int const ToMax = abs(MaxRange - Value);
    int const ToMin = abs(MinRange - Value);
    if (ToMax < ToMin)
    {
        // Nearer the top. Is the cursor, seen from the thumb's start, at least as far along the
        // start->thumb line as the thumb itself?
        Vector Current;
        Vector Start;
        HudTransformPoint(&ThumbXform, &Current, &Zero);
        HudTransformPoint(&ThumbStart, &Start, &Zero);
        Vector CurrentScreen;
        Vector StartScreen;
        if (!ProjectToScreen(Current, &CurrentScreen) || !ProjectToScreen(Start, &StartScreen))
        {
            bPageTowardMin = 1;
            return;
        }
        Vector const Track = Subtract(CurrentScreen, StartScreen);
        float const TrackLength = FastLengthExtended(SquaredLengthZyx(Track));
        Vector const ToMouse = Subtract(Mouse, StartScreen);
        float const MouseLength = FastLengthExtended(SquaredLengthZyx(ToMouse));
        if (TrackLength == 0.0f || MouseLength == 0.0f)
        {
            bPageTowardMin = 1;
            return;
        }
        Vector TrackDir;
        Vector MouseDir;
        HudVec3ScaleByInverse(&TrackDir, &Track, TrackLength);
        HudVec3ScaleByInverse(&MouseDir, &ToMouse, MouseLength);
        double const Dot = (static_cast<double>(MouseDir.z) * TrackDir.z +
                            static_cast<double>(MouseDir.y) * TrackDir.y) +
                           static_cast<double>(MouseDir.x) * TrackDir.x;
        // Unordered gives 0.
        bPageTowardMin = (Dot * MouseLength < TrackLength) ? 1 : 0;
        return;
    }

    // Nearer the bottom (or halfway): the same test from the thumb's end, through this TU's math
    // engine.
    Vector Current;
    Vector End;
    ScrollControlMath()->TransformXform(Current, ThumbXform, Zero);
    ScrollControlMath()->TransformXform(End, ThumbEnd, Zero);
    Vector CurrentScreen;
    Vector EndScreen;
    if (!ProjectToScreen(Current, &CurrentScreen) || !ProjectToScreen(End, &EndScreen))
    {
        bPageTowardMin = 1;
        return;
    }
    Vector const Track = Subtract(CurrentScreen, EndScreen);
    float const TrackLength = FastLengthExtended(SquaredLengthZyx(Track));
    // z is the negated end z (the cursor's z is 0).
    Vector ToMouse;
    ToMouse.x = Mouse.x - EndScreen.x;
    ToMouse.y = Mouse.y - EndScreen.y;
    ToMouse.z = -EndScreen.z;
    float const MouseLength = FastLengthExtended(SquaredLengthZyx(ToMouse));
    if (TrackLength == 0.0f || MouseLength == 0.0f)
    {
        bPageTowardMin = 1;
        return;
    }
    Vector TrackDir;
    HudVec3ScaleByInverse(&TrackDir, &Track, TrackLength);
    // The reciprocal stays double, the products are rounded to float.
    double const InvMouse = 1.0 / static_cast<double>(MouseLength);
    float const MouseDir[3] = {static_cast<float>(ToMouse.x * InvMouse),
                               static_cast<float>(ToMouse.y * InvMouse),
                               static_cast<float>(ToMouse.z * InvMouse)};
    double const Dot = (static_cast<double>(TrackDir.x) * MouseDir[0] +
                        static_cast<double>(MouseDir[1]) * TrackDir.y) +
                       static_cast<double>(MouseDir[2]) * TrackDir.z;
    // Unordered gives 1.
    bPageTowardMin = (Dot * MouseLength < TrackLength) ? 0 : 1;
}

void ScrollControl::PageStep()
{
    // FUN_00580d00. The cursor is read straight from DAT_00616840 / DAT_00616844, as signed.
    Vector const Zero = {0.0f, 0.0f, 0.0f};
    Vector const Mouse = {static_cast<float>(CursorPixelX()), static_cast<float>(CursorPixelY()),
                           0.0f};

    if (bPageTowardMin != 0)
    {
        // Towards the start while the cursor lies short of the thumb.
        Vector Current;
        Vector Start;
        HudTransformPoint(&ThumbXform, &Current, &Zero);
        HudTransformPoint(&ThumbStart, &Start, &Zero);
        Vector CurrentScreen;
        Vector StartScreen;
        if (!ProjectToScreen(Current, &CurrentScreen))
            return;
        if (!ProjectToScreen(Start, &StartScreen))
            return;
        Vector const Track = Subtract(CurrentScreen, StartScreen);
        float const TrackLength = FastLengthStored(SquaredLengthZyx(Track));
        Vector const ToMouse = Subtract(Mouse, StartScreen);
        float const MouseLength = FastLengthStored(SquaredLengthZyx(ToMouse));
        if (TrackLength == 0.0f || MouseLength == 0.0f)
            return;
        Vector TrackDir;
        Vector MouseDir;
        HudVec3ScaleByInverse(&TrackDir, &Track, TrackLength);
        HudVec3ScaleByInverse(&MouseDir, &ToMouse, MouseLength);
        double const Dot = (static_cast<double>(TrackDir.x) * MouseDir.x +
                            static_cast<double>(TrackDir.y) * MouseDir.y) +
                           static_cast<double>(TrackDir.z) * MouseDir.z;
        // Only strictly below goes on.
        if (!(Dot * MouseLength < TrackLength))
            return;
        Value -= PageSize;
        if (Value < MinRange)
            Value = MinRange;
    }
    else
    {
        // Towards the end.
        Vector Current;
        Vector End;
        HudTransformPoint(&ThumbXform, &Current, &Zero);
        HudTransformPoint(&ThumbEnd, &End, &Zero);
        Vector CurrentScreen;
        Vector EndScreen;
        if (!ProjectToScreen(Current, &CurrentScreen))
            return;
        if (!ProjectToScreen(End, &EndScreen))
            return;
        Vector const Track = Subtract(CurrentScreen, EndScreen);
        float const TrackLength = FastLengthStored(SquaredLengthZyx(Track));
        Vector const ToMouse = Subtract(Mouse, EndScreen);
        float const MouseLength = FastLengthExtended(SquaredLengthZyx(ToMouse));
        if (TrackLength == 0.0f || MouseLength == 0.0f)
            return;
        Vector TrackDir;
        Vector MouseDir;
        HudVec3ScaleByInverse(&TrackDir, &Track, TrackLength);
        HudVec3ScaleByInverse(&MouseDir, &ToMouse, MouseLength);
        double const Dot = (static_cast<double>(TrackDir.x) * MouseDir.x +
                            static_cast<double>(TrackDir.y) * MouseDir.y) +
                           static_cast<double>(TrackDir.z) * MouseDir.z;
        if (!(Dot * MouseLength < TrackLength))
            return;
        Value += PageSize;
        if (Value > MaxRange)
            Value = MaxRange;
    }

    UpdateThumb();
    NotifyValue();
}

void ScrollControl::DragThumb(unsigned ScreenX, unsigned ScreenY)
{
    // FUN_005811b0 (RET 8). The cursor projected on the start->end track of the thumb, as a
    // fraction of the range.
    Vector const Zero = {0.0f, 0.0f, 0.0f};
    Vector Start;
    Vector End;
    ScrollControlMath()->TransformXform(Start, ThumbStart, Zero);
    ScrollControlMath()->TransformXform(End, ThumbEnd, Zero);
    Vector StartScreen;
    Vector EndScreen;
    if (!ProjectToScreen(Start, &StartScreen))
        return;
    if (!ProjectToScreen(End, &EndScreen))
        return;

    Vector const Mouse = {static_cast<float>(ScreenX), static_cast<float>(ScreenY), 0.0f};
    Vector const Track = Subtract(EndScreen, StartScreen);
    float const TrackLength = FastLengthExtended(SquaredLengthZyx(Track));
    Vector const ToMouse = Subtract(Mouse, StartScreen);
    float const MouseLength = FastLengthExtended(SquaredLengthZyx(ToMouse));
    if (TrackLength == 0.0f || MouseLength == 0.0f)
        return;

    // Both directions through a double reciprocal; the track's reciprocal is also rounded to
    // float and that float is used again below.
    double const InvTrack = 1.0 / static_cast<double>(TrackLength);
    float const InvTrackFloat = static_cast<float>(InvTrack);
    float const TrackDir[3] = {static_cast<float>(Track.x * InvTrack),
                               static_cast<float>(Track.y * InvTrack),
                               static_cast<float>(Track.z * InvTrack)};
    double const InvMouse = 1.0 / static_cast<double>(MouseLength);
    float const MouseDir[3] = {static_cast<float>(ToMouse.x * InvMouse),
                               static_cast<float>(ToMouse.y * InvMouse),
                               static_cast<float>(ToMouse.z * InvMouse)};
    double const Dot = (static_cast<double>(TrackDir[0]) * MouseDir[0] +
                        static_cast<double>(TrackDir[1]) * MouseDir[1]) +
                       static_cast<double>(TrackDir[2]) * MouseDir[2];

    int const Min = MinRange;
    int const Range = MaxRange - Min;
    double const Scaled = Dot * MouseLength * InvTrackFloat * Range + 0.5;
    Value = static_cast<int>(floor(Scaled)) + Min;
    if (Value > MaxRange)
        Value = MaxRange;
    if (Value < Min)
        Value = Min;

    UpdateThumb();
    NotifyValue(); // inlined in retail
}

void ScrollControl::NotifyValue()
{
    // FUN_00581560.
    if (Parent == nullptr)
        return;
    WinControl* const Owner = Parent->AsWinControl();
    if (Owner == nullptr)
        return;
    int const Current = Value;
    Owner->SetProperty(kWinNotifyScrollValue, reinterpret_cast<uintptr_t>(AsWinControl()),
                       static_cast<uintptr_t>(Current));
}

bool ScrollControl::Create(Win3D* NewParent, void* Data)
{
    // LAB_005815a0 (RET 8): Win3D::Create's body inlined, then the [ScrollControl] keys from the
    // INI_Reader in Data.
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & ~kWin3DUnregistered) | kWin3DActive | kWin3DVisible);

    INI_Reader* const Reader = static_cast<INI_Reader*>(Data);
    if (Reader != nullptr)
    {
        while (Reader->read_value())
        {
            if (Reader->is_value("back_mesh"))
            {
                LoadPartMesh(this, &Back.Holder, Reader->get_value_string(), &Back.Xform);
            }
            else if (Reader->is_value("back_offset"))
            {
                ReadVector(Reader, &Back.Offset);
                UpdateLayout();
                PushInstancePosition(&Back.Holder, &Back.Xform.Translation);
            }
            else if (Reader->is_value("back_hp"))
            {
                // The hardpoint is asked of the Create argument, not of the linked parent.
                SetIdentityTransform(&Back.Hp);
                if (NewParent != nullptr)
                    AskHardpoint(NewParent, Reader->get_value_string(), &Back.Hp);
            }
            else if (Reader->is_value("back_mouse"))
            {
                ReadVector(Reader, &Back.MouseSize);
            }
            else if (Reader->is_value("back_no_mesh_render"))
            {
                Back.bNoMeshRender = ReadFlag(Reader);
            }
            else if (Reader->is_value("nextb_mesh"))
            {
                LoadPartMesh(this, &Next.Holder, Reader->get_value_string(), &Next.Xform);
            }
            else if (Reader->is_value("nextb_offset"))
            {
                ReadVector(Reader, &Next.Offset);
                UpdateLayout();
                PushInstancePosition(&Next.Holder, &Next.Xform.Translation);
            }
            else if (Reader->is_value("nextb_hp"))
            {
                SetIdentityTransform(&Next.Hp);
                if (NewParent != nullptr)
                    AskHardpoint(NewParent, Reader->get_value_string(), &Next.Hp);
            }
            else if (Reader->is_value("nextb_mouse"))
            {
                ReadVector(Reader, &Next.MouseSize);
            }
            else if (Reader->is_value("nextb_no_mesh_render"))
            {
                Next.bNoMeshRender = ReadFlag(Reader);
            }
            else if (Reader->is_value("prevb_mesh"))
            {
                LoadPartMesh(this, &Prev.Holder, Reader->get_value_string(), &Prev.Xform);
            }
            else if (Reader->is_value("prevb_offset"))
            {
                ReadVector(Reader, &Prev.Offset);
                UpdateLayout();
                PushInstancePosition(&Prev.Holder, &Prev.Xform.Translation);
            }
            else if (Reader->is_value("prevb_hp"))
            {
                SetIdentityTransform(&Prev.Hp);
                if (NewParent != nullptr)
                    AskHardpoint(NewParent, Reader->get_value_string(), &Prev.Hp);
            }
            else if (Reader->is_value("prevb_mouse"))
            {
                ReadVector(Reader, &Prev.MouseSize);
            }
            else if (Reader->is_value("prevb_no_mesh_render"))
            {
                Prev.bNoMeshRender = ReadFlag(Reader);
            }
            else if (Reader->is_value("thumb_mesh"))
            {
                LoadPartMesh(this, &ThumbHolder, Reader->get_value_string(), &ThumbXform);
            }
            else if (Reader->is_value("thumb_offset0"))
            {
                ReadVector(Reader, &ThumbOffset0);
                UpdateLayout();
                PushInstancePosition(&ThumbHolder, &ThumbXform.Translation);
            }
            else if (Reader->is_value("thumb_offset1"))
            {
                ReadVector(Reader, &ThumbOffset1);
                UpdateLayout();
                PushInstancePosition(&ThumbHolder, &ThumbXform.Translation);
            }
            else if (Reader->is_value("thumb_hp0"))
            {
                SetIdentityTransform(&ThumbHp0);
                if (NewParent != nullptr)
                    AskHardpoint(NewParent, Reader->get_value_string(), &ThumbHp0);
            }
            else if (Reader->is_value("thumb_hp1"))
            {
                SetIdentityTransform(&ThumbHp1);
                if (NewParent != nullptr)
                    AskHardpoint(NewParent, Reader->get_value_string(), &ThumbHp1);
            }
            else if (Reader->is_value("thumb_mouse"))
            {
                ReadVector(Reader, &ThumbMouse);
            }
            else if (Reader->is_value("thumb_no_mesh_render"))
            {
                bThumbNoMeshRender = ReadFlag(Reader);
            }
            else if (Reader->is_value("min_range"))
            {
                MinRange = static_cast<int>(Reader->get_value_float(0)); // ftol
            }
            else if (Reader->is_value("max_range"))
            {
                MaxRange = static_cast<int>(Reader->get_value_float(0));
            }
            else if (Reader->is_value("page_size"))
            {
                PageSize = static_cast<int>(Reader->get_value_float(0));
            }
            else if (Reader->is_value("slider_behavior"))
            {
                bSliderBehavior = ReadFlag(Reader);
            }
            else if (Reader->is_value("no_z_enable"))
            {
                bNoZEnable = ReadFlag(Reader);
            }
        }
    }

    // The linked parent re-applies its own position.
    Win3D* const LinkedParent = Parent;
    if (LinkedParent != nullptr)
        LinkedParent->SetPosition(&LinkedParent->Position);
    return true;
}

void ScrollControl::Init(char const* BackMesh, Vector const* BackOffset, char const* BackHp,
                         Vector const* BackMouse, bool bBackNoMeshRender, char const* NextMesh,
                         Vector const* NextOffset, char const* NextHp, Vector const* NextMouse,
                         bool bNextNoMeshRender, char const* PrevMesh, Vector const* PrevOffset,
                         char const* PrevHp, Vector const* PrevMouse, bool bPrevNoMeshRender,
                         char const* ThumbMesh, Vector const* NewThumbOffset0,
                         Vector const* NewThumbOffset1, char const* NewThumbHp0,
                         char const* NewThumbHp1, Vector const* NewThumbMouse,
                         bool bNewThumbNoMeshRender, int NewMinRange, int NewMaxRange,
                         int NewPageSize, bool bNewSliderBehavior, bool bNewNoZEnable)
{
    // FUN_00582120 (RET 0x6c). Hardpoints here are asked of the linked parent (+4).
    if (BackMesh != nullptr)
        LoadPartMesh(this, &Back.Holder, BackMesh, &Back.Xform);
    Back.Offset = {BackOffset->x, BackOffset->y, BackOffset->z};
    UpdateLayout();
    PushInstancePosition(&Back.Holder, &Back.Xform.Translation);
    if (BackHp != nullptr)
    {
        SetIdentityTransform(&Back.Hp);
        if (Parent != nullptr)
            AskHardpoint(Parent, BackHp, &Back.Hp);
    }
    Back.MouseSize = {BackMouse->x, BackMouse->y, BackMouse->z};
    Back.bNoMeshRender = bBackNoMeshRender ? 1 : 0;

    if (NextMesh != nullptr)
        LoadPartMesh(this, &Next.Holder, NextMesh, &Next.Xform);
    Next.Offset = {NextOffset->x, NextOffset->y, NextOffset->z};
    UpdateLayout();
    PushInstancePosition(&Next.Holder, &Next.Xform.Translation);
    if (NextHp != nullptr)
    {
        SetIdentityTransform(&Next.Hp);
        if (Parent != nullptr)
            AskHardpoint(Parent, NextHp, &Next.Hp);
    }
    Next.MouseSize = {NextMouse->x, NextMouse->y, NextMouse->z};
    Next.bNoMeshRender = bNextNoMeshRender ? 1 : 0;

    // Prev: all of it only with a mesh name.
    if (PrevMesh != nullptr)
    {
        LoadPartMesh(this, &Prev.Holder, PrevMesh, &Prev.Xform);
        Prev.Offset = {PrevOffset->x, PrevOffset->y, PrevOffset->z};
        UpdateLayout();
        PushInstancePosition(&Prev.Holder, &Prev.Xform.Translation);
        if (PrevHp != nullptr)
        {
            SetIdentityTransform(&Prev.Hp);
            if (Parent != nullptr)
                AskHardpoint(Parent, PrevHp, &Prev.Hp);
        }
        Prev.MouseSize = {PrevMouse->x, PrevMouse->y, PrevMouse->z};
        Prev.bNoMeshRender = bPrevNoMeshRender ? 1 : 0;
    }

    if (ThumbMesh != nullptr)
        LoadPartMesh(this, &ThumbHolder, ThumbMesh, &ThumbXform);
    ThumbOffset0 = {NewThumbOffset0->x, NewThumbOffset0->y, NewThumbOffset0->z};
    UpdateLayout();
    PushInstancePosition(&ThumbHolder, &ThumbXform.Translation);
    ThumbOffset1 = {NewThumbOffset1->x, NewThumbOffset1->y, NewThumbOffset1->z};
    UpdateLayout();
    PushInstancePosition(&ThumbHolder, &ThumbXform.Translation);
    if (NewThumbHp0 != nullptr)
    {
        SetIdentityTransform(&ThumbHp0);
        if (Parent != nullptr)
            AskHardpoint(Parent, NewThumbHp0, &ThumbHp0);
    }
    if (NewThumbHp1 != nullptr)
    {
        SetIdentityTransform(&ThumbHp1);
        if (Parent != nullptr)
            AskHardpoint(Parent, NewThumbHp1, &ThumbHp1);
    }

    ThumbMouse ={NewThumbMouse->x, NewThumbMouse->y, NewThumbMouse->z};
    bThumbNoMeshRender = bNewThumbNoMeshRender ? 1 : 0;
    MinRange = NewMinRange;
    MaxRange = NewMaxRange;
    PageSize = NewPageSize;
    bSliderBehavior = bNewSliderBehavior ? 1 : 0;
    bNoZEnable = bNewNoZEnable ? 1 : 0;
}

bool ScrollControl::HitTest(Vector const* Ray)
{
    // LAB_00583130: any button, or a valid thumb.
    if (RayHits(Back.MouseSize, Back.Xform, Ray))
        return true;
    if (RayHits(Next.MouseSize, Next.Xform, Ray))
        return true;
    if (RayHits(Prev.MouseSize, Prev.Xform, Ray))
        return true;
    if (bThumbValid != 0 && RayHits(ThumbMouse, ThumbXform, Ray))
        return true;
    return false;
}

void ScrollControl::SetPosition(Vector const* NewPosition)
{
    // LAB_00582aa0.
    WinControl::SetPosition(NewPosition);
    UpdateLayout();
}

void ScrollControl::SetOrientation(Matrix const* NewOrientation)
{
    // FUN_00582ac0: straight into Win3D's orientation (not WinControl's SetOrientation).
    memcpy(&Orientation, NewOrientation, sizeof(Orientation));
    UpdateLayout();
}

void ScrollControl::Update()
{
    // FUN_00582ae0: the children first.
    WinControl::Update();

    if (bThumbValid == 0)
    {
        Win3DRemoveCapture(this);
        G_ScrollControlDragging = 0;
        bDragging = 0;
        bPaging = 0;
        bNextPressed = 0;
        bPrevPressed = 0;
        return;
    }

    if (bDragging != 0)
    {
        DragThumb(static_cast<unsigned>(CursorPixelX()), static_cast<unsigned>(CursorPixelY()));
        return;
    }

    // Auto-repeat every 0.3 s (DAT_005c9858); only strictly later goes on.
    if (bPaging != 0)
    {
        if (GetClientElapsedTime() - static_cast<double>(RepeatTime) > 0.3)
        {
            PageStep();
            RepeatTime = static_cast<float>(GetClientElapsedTime());
        }
        return;
    }

    if (bNextPressed != 0)
    {
        if (!(GetClientElapsedTime() - static_cast<double>(RepeatTime) > 0.3))
            return;
        if (Value < MaxRange)
        {
            Value += Step;
            if (Value > MaxRange)
                Value = MaxRange;
            UpdateThumb();
            NotifyValue(); // inlined in retail
        }
        RepeatTime = static_cast<float>(GetClientElapsedTime());
        return;
    }

    if (bPrevPressed == 0)
        return;

    if (!(GetClientElapsedTime() - static_cast<double>(RepeatTime) > 0.3))
        return;
    if (Value > MinRange)
    {
        Value -= Step;
        if (Value < MinRange)
            Value = MinRange;
        UpdateThumb();
        NotifyValue();
    }
    RepeatTime = static_cast<float>(GetClientElapsedTime());
}

void ScrollControl::Draw()
{
    // LAB_005826e0: "[INTERFACE]" (option 6) and "NEURONET" (option 9), the children, then the
    // parts -- only while the thumb is valid.
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
        return;
    if (GetGameOption(kGameOptNeuronet) == 0)
        return;
    DrawChildren();
    if (bThumbValid == 0)
        return;

    // The idle colour DAT_00679bb0 and the hot colour.
    unsigned long const Idle = G_NnPaletteCyan;
    unsigned char const HotBytes[4] = {0xf5, 0xea, 0x52, 0xff};
    unsigned long Hot = 0;
    memcpy(&Hot, HotBytes, sizeof(Hot));
    FSpaceCamera* const Camera = Win3DGetCamera();

    // The back plate: the untinted pass unless back_no_mesh_render, then depth test off and
    // always the idle-tinted pass.
    if (Back.Holder.HasInstance())
    {
        if (Back.bNoMeshRender == 0)
            DrawPartMesh(&Back.Holder, &Idle);
        Win3DDisable(kWin3DDepthTest);
        unsigned long Tint = Idle;
        SubmitWin3DTintedEngineInstanceWrapped(Camera, Back.Holder.EngineInstance, 1,
                                               reinterpret_cast<unsigned char const*>(&Tint), 1,
                                               1.0f, 0);
    }

    // Next: hot while pressed or hovered; no tinted pass.
    if (Next.Holder.HasInstance())
    {
        unsigned long const* const Color =
            (bNextPressed != 0 || bNextHover != 0) ? &Hot : &Idle;
        if (Next.bNoMeshRender == 0)
            DrawPartMesh(&Next.Holder, Color);
        Win3DDisable(kWin3DDepthTest);
    }

    if (Prev.Holder.HasInstance())
    {
        unsigned long const* const Color =
            (bPrevPressed != 0 || bPrevHover != 0) ? &Hot : &Idle;
        if (Prev.bNoMeshRender == 0)
            DrawPartMesh(&Prev.Holder, Color);
        Win3DDisable(kWin3DDepthTest);
    }

    // The thumb: untinted in the idle colour unless thumb_no_mesh_render, then tinted hot while
    // dragged or hovered, then depth test off.
    if (bThumbValid == 0)
        return;
    if (!ThumbHolder.HasInstance())
        return;
    if (bThumbNoMeshRender == 0)
        DrawPartMesh(&ThumbHolder, &Idle);
    unsigned long Tint = (bDragging != 0 || bThumbHover != 0) ? Hot : Idle;
    SubmitWin3DTintedEngineInstanceWrapped(Camera, ThumbHolder.EngineInstance, 1,
                                           reinterpret_cast<unsigned char const*>(&Tint), 1, 1.0f,
                                           0);
    Win3DDisable(kWin3DDepthTest);
}

void ScrollControl::MouseOver(Vector const* Ray)
{
    // LAB_00583120: a virtual tail jump to MouseEnter.
    MouseEnter(Ray);
}

void ScrollControl::MouseEnter(Vector const* Ray)
{
    // LAB_005830a0 (RET 4): the hover bytes; a hovered valid thumb masks both buttons.
    bNextHover = RayHits(Next.MouseSize, Next.Xform, Ray) ? 1 : 0;
    bPrevHover = RayHits(Prev.MouseSize, Prev.Xform, Ray) ? 1 : 0;
    bThumbHover = 0;
    if (bThumbValid == 0)
        return;
    bThumbHover = RayHits(ThumbMouse, ThumbXform, Ray) ? 1 : 0;
    if (bThumbHover != 0)
    {
        bNextHover = 0;
        bPrevHover = 0;
    }
}

void ScrollControl::MouseLeave(Vector const* Ray)
{
    // LAB_00583120.
    MouseEnter(Ray);
}

bool ScrollControl::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00582c50 (RET 4). Without a valid thumb nothing is taken; with one every event is.
    if (bThumbValid == 0)
        return false;

    // A held part lets go on the left-button release.
    bool const bRelease =
        Event->Button == kWin3DMouseLeft && Event->Action == kWin3DMouseRelease;
    if (bDragging != 0)
    {
        if (bRelease)
        {
            Win3DRemoveCapture(this);
            bDragging = 0;
            G_ScrollControlDragging = 0;
        }
        return true;
    }
    if (bNextPressed != 0)
    {
        if (bRelease)
        {
            Win3DRemoveCapture(this);
            bNextPressed = 0;
        }
        return true;
    }
    if (bPrevPressed != 0)
    {
        if (bRelease)
        {
            Win3DRemoveCapture(this);
            bPrevPressed = 0;
        }
        return true;
    }
    if (bPaging != 0)
    {
        if (bRelease)
        {
            Win3DRemoveCapture(this);
            bPaging = 0;
        }
        return true;
    }

    // Only {-1, 2} (the press) goes on.
    if (Event->Button != kWin3DMouseLeft || Event->Action != kWin3DMousePress)
        return true;

    // The cursor ray: the pixel onto the near plane of the Win3D camera DAT_0067dbf8 through a
    // copy of its orientation, normalised with a real sqrt (unlike the fast inverse root above).
    FSpaceCamera* const Cam = Win3DGetCamera();
    int const CursorX = CursorPixelX();
    int const CursorY = CursorPixelY();
    Vector Local;
    Local.x = static_cast<float>((static_cast<double>(CursorX) - Cam->ScreenCenterX) /
                                 Cam->XScale * Cam->ZNear);
    Local.y = static_cast<float>((static_cast<double>(CursorY) - Cam->ScreenCenterY) /
                                 Cam->YScale * Cam->ZNear);
    Local.z = -Cam->ZNear;
    Matrix CamOrientation;
    memcpy(&CamOrientation, &Cam->World, sizeof(CamOrientation));
    Vector World;
    ScrollControlMath()->TransformMatrix(World, CamOrientation, Local);
    double const Inv = 1.0 / sqrt((static_cast<double>(World.x) * World.x +
                                   static_cast<double>(World.y) * World.y) +
                                  static_cast<double>(World.z) * World.z);
    Vector Ray;
    Ray.x = static_cast<float>(World.x * Inv);
    Ray.y = static_cast<float>(World.y * Inv);
    Ray.z = static_cast<float>(World.z * Inv);

    bool bStartDrag = false;
    if (bThumbValid != 0 && RayHits(ThumbMouse, ThumbXform, &Ray))
    {
        Win3DPushCapture(this);
        bStartDrag = true;
    }
    else if (RayHits(Next.MouseSize, Next.Xform, &Ray))
    {
        // One step up at once, then auto-repeat from Update.
        Win3DPushCapture(this);
        if (Value < MaxRange)
        {
            Value += Step;
            if (Value > MaxRange)
                Value = MaxRange;
            UpdateThumb();
            NotifyValue();
        }
        bNextPressed = 1;
        RepeatTime = static_cast<float>(GetClientElapsedTime());
        return true;
    }
    else if (RayHits(Prev.MouseSize, Prev.Xform, &Ray))
    {
        Win3DPushCapture(this);
        if (Value > MinRange)
        {
            Value -= Step;
            if (Value < MinRange)
                Value = MinRange;
            UpdateThumb();
            NotifyValue();
        }
        bPrevPressed = 1;
        RepeatTime = static_cast<float>(GetClientElapsedTime());
        return true;
    }
    else
    {
        // The back plate.
        if (bThumbValid == 0)
            return true;
        if (!RayHits(Back.MouseSize, Back.Xform, &Ray))
            return true;
        Win3DPushCapture(this);
        if (bSliderBehavior == 0)
        {
            // Page, and keep paging from Update.
            bPaging = 1;
            RepeatTime = static_cast<float>(GetClientElapsedTime());
            ChoosePageDirection(static_cast<unsigned>(CursorPixelX()),
                                static_cast<unsigned>(CursorPixelY()));
            PageStep();
            return true;
        }
        bStartDrag = true;
    }

    if (bStartDrag)
    {
        bDragging = 1;
        G_ScrollControlDragging = 1;
        DragThumb(static_cast<unsigned>(CursorPixelX()), static_cast<unsigned>(CursorPixelY()));
    }
    return true;
}

void ScrollControl::RefreshAttachment()
{
    // FUN_00401e90 (empty): a ScrollControl does not follow a hardpoint the way WinControl's
    // does.
}

intptr_t ScrollControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_005831b0 (RET 0xc).
    switch (Msg)
    {
    case kWinPropOffset:
    {
        // WinControl::Offset (+0xa8) from the vector at A, then the layout.
        float const* const NewOffset = reinterpret_cast<float const*>(A);
        Offset.x = NewOffset[0];
        Offset.y = NewOffset[1];
        Offset.z = NewOffset[2];
        UpdateLayout();
        return 0;
    }
    case kScrollPropSetRange:
    {
        // The range, ordered, the value clamped into it, reported.
        int const First = static_cast<int>(A);
        int const Second = static_cast<int>(B);
        MinRange = First;
        MaxRange = Second;
        if (First > Second)
        {
            MaxRange = First;
            MinRange = Second;
        }
        if (Value > MaxRange)
            Value = MaxRange;
        if (Value < MinRange)
            Value = MinRange;
        UpdateThumb();
        NotifyValue();
        return 0;
    }
    case kScrollPropSetValue:
    {
        // The value, not below the minimum; above the maximum only when the range is not empty.
        // Not reported.
        Value = static_cast<int>(A);
        if (Value < MinRange)
            Value = MinRange;
        if (Value > MaxRange && MaxRange > MinRange)
            Value = MaxRange;
        UpdateThumb();
        return 0;
    }
    case kScrollPropGetMax:
        return MaxRange;
    case kScrollPropGetMin:
        return MinRange;
    case kScrollPropGetValue:
        return Value;
    case kScrollPropSetPage:
        // The page size, and the button step (0 means 1).
        PageSize = static_cast<int>(A);
        Step = (B != 0) ? static_cast<int>(B) : 1;
        return 0;
    case kScrollPropGetPage:
        return PageSize;
    default:
        return WinControl::SetProperty(Msg, A, B);
    }
}

void ScrollControl::AddAttachOffset(float const* Delta)
{
    // LAB_00583390 (RET 4): Delta onto the translation of every hardpoint transform; nothing
    // is re-laid out.
    Back.Hp.Translation.x += Delta[0];
    Back.Hp.Translation.y += Delta[1];
    Back.Hp.Translation.z += Delta[2];
    Next.Hp.Translation.x += Delta[0];
    Next.Hp.Translation.y += Delta[1];
    Next.Hp.Translation.z += Delta[2];
    Prev.Hp.Translation.x += Delta[0];
    Prev.Hp.Translation.y += Delta[1];
    Prev.Hp.Translation.z += Delta[2];
    ThumbHp0.Translation.x += Delta[0];
    ThumbHp0.Translation.y += Delta[1];
    ThumbHp0.Translation.z += Delta[2];
    ThumbHp1.Translation.x += Delta[0];
    ThumbHp1.Translation.y += Delta[1];
    ThumbHp1.Translation.z += Delta[2];
}

WinControl* WinControl::CreateScrollControl(
    char const* Name, char const* BackMesh, Vector const* BackOffset, char const* BackHp,
    Vector const* BackMouse, bool bBackNoMeshRender, char const* NextMesh, Vector const* NextOffset,
    char const* NextHp, Vector const* NextMouse, bool bNextNoMeshRender, char const* PrevMesh,
    Vector const* PrevOffset, char const* PrevHp, Vector const* PrevMouse, bool bPrevNoMeshRender,
    char const* ThumbMesh, Vector const* ThumbOffset0, Vector const* ThumbOffset1,
    char const* ThumbHp0, char const* ThumbHp1, Vector const* ThumbMouse,
    bool bThumbNoMeshRender, int MinRange, int MaxRange, int PageSize, bool bSliderBehavior,
    bool bNoZEnableArg)
{
    // FUN_005a0ef0 (RET 0x70).
    Win3D* const Created = WinFactoryCreate(Name, "ScrollControl", this, nullptr);
    if (Created != nullptr)
    {
        static_cast<ScrollControl*>(Created)->Init(
            BackMesh, BackOffset, BackHp, BackMouse, bBackNoMeshRender, NextMesh, NextOffset,
            NextHp, NextMouse, bNextNoMeshRender, PrevMesh, PrevOffset, PrevHp, PrevMouse,
            bPrevNoMeshRender, ThumbMesh, ThumbOffset0, ThumbOffset1, ThumbHp0, ThumbHp1,
            ThumbMouse, bThumbNoMeshRender, MinRange, MaxRange, PageSize, bSliderBehavior,
            bNoZEnableArg);
    }
    return static_cast<WinControl*>(Created);
}
