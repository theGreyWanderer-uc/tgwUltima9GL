namespace U9GL.Core.Tests;

[TestClass]
public sealed class FaultsTests
{
    [TestMethod]
    public void NamesReadInsideASentence()
    {
        Assert.AreEqual("crash when loading a save", Faults.F3.InSentence);
        Assert.AreEqual("hostile NPCs that never attack", Faults.F4.InSentence);
    }
}
