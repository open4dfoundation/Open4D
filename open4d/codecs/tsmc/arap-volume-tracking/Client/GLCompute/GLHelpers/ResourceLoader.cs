//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System.IO;

namespace Client.GLCompute.GLHelpers
{
    static class ResourceLoader
    {
        public static string Load(string resourecePath)
        {
            using var stream = typeof(ResourceLoader).Assembly.GetManifestResourceStream(resourecePath);
            using var reader = new StreamReader(stream);
            var text = reader.ReadToEnd();
            reader.Close();
            stream.Close();
            return text;
        }
    }
}
