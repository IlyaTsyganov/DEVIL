namespace Devil.Game;

public sealed record SupportPlaneCut(string Name, float NormalA, float NormalB, float? TangentAngle, float? NormalAngle)
{
    public float Magnitude => MathF.Sqrt(NormalA * NormalA + NormalB * NormalB);
}
public sealed record SupportPlane(MotionVector Normal, float? Slope, IReadOnlyList<SupportPlaneCut> Cuts)
{
    public static SupportPlane? From(MotionSample? sample)
    {
        if (sample is not { Contact: true, Normal.Finite: true } || sample.Normal.Length is < .9f or > 1.1f) return null;
        float length = sample.Normal.Length;
        var n = new MotionVector(sample.Normal.X / length, sample.Normal.Y / length, sample.Normal.Z / length);
        float? slope = sample.Up is { Finite: true, Length: > .9f and < 1.1f } up
            ? MathF.Acos(Math.Clamp((n.X * up.X + n.Y * up.Y + n.Z * up.Z) / up.Length, -1, 1)) * 180 / MathF.PI : null;
        SupportPlaneCut Cut(string name, float a, float b)
        {
            if (a * a + b * b < 1e-10f) return new(name, a, b, null, null);
            float x = b, y = -a;
            if (x < 0 || (Math.Abs(x) < 1e-7f && y < 0)) { x = -x; y = -y; }
            return new(name, a, b, MathF.Atan2(y, x) * 180 / MathF.PI, MathF.Atan2(b, a) * 180 / MathF.PI);
        }
        return new(n, slope, [Cut("XY", n.X, n.Y), Cut("XZ", n.X, n.Z), Cut("YZ", n.Y, n.Z)]);
    }
}
