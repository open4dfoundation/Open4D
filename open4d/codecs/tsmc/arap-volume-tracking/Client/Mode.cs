//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;

namespace Client
{
    [Flags]
    public enum Mode : long
    {
        stats = 0x80000000,
        /// <summary>
        /// Compute volume grid else load it from the file
        /// </summary>
        preprocessVG = 0x01,

        /// <summary>
        /// Compute volumegrid on GPU
        /// </summary>
        preprocessVG_GPU = 0x02,

        /// <summary>
        /// Compute centers else load it from the file
        /// </summary>
        preprocessPC = 0x10,

        /// <summary>
        /// Compute centers on GPU (implicates vg on GPU)
        /// </summary>
        preprocessPC_GPU = 0x23,

        preprocess = preprocessVG | preprocessPC, /* compatbility alias */
        preprocessWithStats = preprocessVG | stats,
        preprocess_GPU = preprocess | preprocessVG_GPU | preprocessPC_GPU,
        preprocess_GPUWithStats = preprocess_GPU | stats, /* compatbility alias */
        pc = preprocessPC,
        pcWithStats = pc | stats, /* compatbility alias */

        /// <summary>
        /// Do postprocess
        /// </summary>
        tracking = 0x0100,
        trackingWithStats = tracking | stats,

        /// <summary>
        /// Do preprocess and postprocess
        /// </summary>
        process = preprocess | tracking | stats,
        //processWithStats = process | stats,

        //evaluation = 0x010000,
        gtcmp = 0x010000,
        improvement = 0x020000,
        IIR = 0x040000
    };

    public static class ModeExt
    {
        public static bool Has(this Mode me, Mode that) => (me & that) == that;
        public static bool In(this Mode me, Mode that) => (that & me) == me;
        public static bool Any(this Mode me, Mode that) => (int)(me & that) != 0;
    }
}



