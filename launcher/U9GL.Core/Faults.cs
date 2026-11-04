namespace U9GL.Core;

public sealed record Fault(string Id, string Name, string Summary)
{
    public string InSentence => char.ToLowerInvariant(Name[0]) + Name[1..];
}

public static class Faults
{
    public static Fault F1 { get; } = new("F-1",
        "Crash at startup when the OpenGL renderer is set but U9GL is missing",
        "Options.ini selects rasterizer 5, which exists only while U9GL is loaded.");

    public static Fault F2 { get; } = new("F-2",
        "Crash when the game runs out of memory",
        "The game does not check a failed memory allocation; an early U9GL build used enough memory to reach it.");

    public static Fault F3 { get; } = new("F-3",
        "Crash when loading a save",
        "An actor's list of targets starts with leftover memory, and the game reads through it.");

    public static Fault F4 { get; } = new("F-4",
        "Hostile NPCs that never attack",
        "An actor's attack countdown starts with leftover memory, and a negative value never counts down.");

    public static Fault F5 { get; } = new("F-5",
        "Crash when a spellcaster teleports",
        "The teleport spell uses its subject after the game has released it.");

    public static Fault F6 { get; } = new("F-6",
        "Crash when quitting",
        "An actor's destructor calls into a part of it that is already freed.");

    public static Fault F7 { get; } = new("F-7",
        "Crash while flying",
        "While the world streams in and out quickly, the game passes a missing object to its reference cleanup.");

    public static Fault F8 { get; } = new("F-8",
        "Crash when a cutscene starts or is skipped",
        "A cutscene texture slot is used before it is checked; seen as a cutscene began and when Esc skipped one.");

    public static Fault F9 { get; } = new("F-9",
        "Crash when loading certain saves",
        "The save was made with different world files, and the game reads an object slot that is empty in these.");

    public static Fault F11 { get; } = new("F-11",
        "Crash when a spell lands after its caster is gone",
        "A fireball or other spell keeps flying after its caster is unloaded, and reads the missing caster when it lands.");

    public static Fault F12 { get; } = new("F-12",
        "Crash when a creature's attack lands after the creature is gone",
        "A creature's breath, beam or missile, or a spell's end, calls back into a creature that is no longer there.");

    public static Fault F13 { get; } = new("F-13",
        "Slow-motion animation after Windows has been on for about 25 days",
        "The game's clocks count from when Windows started, and past 24.86 days animation crawls while walking speed stays normal.");

    public static Fault F15 { get; } = new("F-15",
        "Crash when a dragon attacks a target it has already lost",
        "A dragon that has just killed the Avatar breathes again, at a target it no longer has.");

    public static Fault F16 { get; } = new("F-16",
        "Crash after the Avatar dies, while creatures are still around him",
        "Creatures go on turning toward and measuring the distance to the Avatar after the game has removed him.");

    public static Fault F17 { get; } = new("F-17",
        "Crash in the graphics driver when quitting",
        "As the game closed, U9GL shut its OpenGL renderer down too late, and NVIDIA's driver stopped the game.");

    public static Fault F18 { get; } = new("F-18",
        "Crash while a shrine is being cleansed",
        "The cleansing reveals the shrine's own rune and item without checking they are still there.");

    public static Fault F19 { get; } = new("F-19",
        "Crash in the scripted fight between Lord British and Blackthorn",
        "When his Rings of Fire overlap, the game takes a burning ring for a spell and deletes it as one.");

    public static IReadOnlyList<Fault> All { get; } = [F1, F2, F3, F4, F5, F6, F7, F8, F9, F11, F12, F13, F15, F16, F17, F18, F19];
}
