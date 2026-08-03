//
// Copyright (c) 2022,2023 Jan Dvoøák, Zuzana Káèereková, Petr Vanìèek, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.ComponentModel;
using System.Drawing;

namespace Framework
{
    /// <summary>
    /// Represent color in RGBA format.
    /// </summary>
    public struct ColorRGBA
    {
        /// <summary>
        /// Red channel.
        /// </summary>
        private byte red;
        /// <summary>
        /// Green channel.
        /// </summary>
        private byte green;
        /// <summary>
        /// Blue channel.
        /// </summary>
        private byte blue;
        /// <summary>
        /// Alpha channel.
        /// </summary>
        private byte alpha;

        /// <summary>
        /// Gets/sets red channel.
        /// </summary>
        [Category("Color value"), Description("Gets/sets red channel.")]
        public byte Red { get { return red; } set { red = value; } }

        /// <summary>
        /// Gets/sets green channel.
        /// </summary>
        [Category("Color value"), Description("Gets/sets green channel.")]
        public byte Green { get { return green; } set { green = value; } }

        /// <summary>
        /// Gets/sets blue channel.
        /// </summary>
        [Category("Color value"), Description("Gets/sets blue channel.")]
        public byte Blue { get { return blue; } set { blue = value; } }

        /// <summary>
        /// Gets/sets alpha channel.
        /// </summary>
        [Category("Color value"), Description("Gets/sets alpha channel.")]
        public byte Alpha { get { return alpha; } set { alpha = value; } }


        // ===== Constructors =============================================================
        #region Constructors

        /// <summary>
        /// Constructor create an instance of Color with alpha value 255 (opaque)
        /// </summary>
        /// <param name="red">Red chanel (0 to 255).</param>
        /// <param name="green">Green chanel(0 to 255).</param>
        /// <param name="blue">Blue chanel (0 to 255).</param>
        public ColorRGBA(byte red, byte green, byte blue)
        {
            this.red = red;
            this.green = green;
            this.blue = blue;
            this.alpha = 255;
        } // ColorRGBA()

        /// <summary>
        /// Constructor create an instance of Color with implicit alpha value 1.0 (opaque)
        /// </summary>
        /// <param name="red">Red chanel (0.0 to 1.0).</param>
        /// <param name="green">Green chanel(0.0 to 1.0).</param>
        /// <param name="blue">Blue chanel (0.0 to 1.0).</param>
        public ColorRGBA(float red, float green, float blue)
        {
            this.red = (byte)(red * 255.0f);
            this.green = (byte)(green * 255.0f);
            this.blue = (byte)(blue * 255.0f);
            this.alpha = (byte)(1.0F * 255.0f);
        } // ColorRGBA()

        /// <summary>
        /// Constructor create an instance of Color with explicit alpha (opaque)
        /// </summary>
        /// <param name="red">Red chanel (0 to 255).</param>
        /// <param name="green">Green chanel(0 to 255).</param>
        /// <param name="blue">Blue chanel (0 to 255).</param>
        /// <param name="alpha">Alpha chanel (transparency)(0 to 255).</param>
        public ColorRGBA(byte red, byte green, byte blue, byte alpha)
        {
            this.red = red;
            this.green = green;
            this.blue = blue;
            this.alpha = alpha;
        } // ColorRGBA()

        /// <summary>
        /// Constructor create an instance of Color with explicit alpha (opaque)
        /// </summary>
        /// <param name="red">Red chanel (0.0 to 1.0).</param>
        /// <param name="green">Green chanel(0.0 to 1.0).</param>
        /// <param name="blue">Blue chanel (0.0 to 1.0).</param>
        /// <param name="alpha">Alpha chanel (transparency)(0.0 to 1.0).</param>
        public ColorRGBA(float red, float green, float blue, float alpha)
        {
            this.red = (byte)(red * 255.0f);
            this.green = (byte)(green * 255.0f);
            this.blue = (byte)(blue * 255.0f);
            this.alpha = (byte)(alpha * 255.0f);
        } // ColorRGBA()

        #endregion

        /// <summary>
        /// Returns color as an intereger, red at highest bits, blue at lowest bits.
        /// </summary>
        /// <returns>Color represented as a single integer.</returns>
        public int GetInt()
        {
            return ((int)this.Red * 256 * 256 + this.Green * 256 + this.Blue);
        }

        /// <summary>
        /// Get/Set red channel as float (0 to 1).
        /// </summary>
        [Category("Float color value"), Description("Get/Set red channel as float (0 to 1).")]
        public float RedF
        {
            get
            {
                return ((float)this.Red) / 255.0f;
            }
            set
            {
                this.Red = (byte)(value * 255.0f);
            }
        } // RedF    

        /// <summary>
        /// Get/Set green channel as float (0 to 1).
        /// </summary>
        [Category("Float color value"), Description("Get/Set green channel as float (0 to 1).")]
        public float GreenF
        {
            get
            {
                return ((float)this.Green) / 255.0f;
            }
            set
            {
                this.Green = (byte)(value * 255.0f);
            }
        } // GreenF    

        /// <summary>
        /// Get/Set blue channel as float (0 to 1).
        /// </summary>
        [Category("Float color value"), Description("Get/Set blue channel as float (0 to 1).")]
        public float BlueF
        {
            get
            {
                return ((float)this.Blue) / 255.0f;
            }
            set
            {
                this.Blue = (byte)(value * 255.0f);
            }
        } // BlueF

        /// <summary>
        /// Get/Set alpha channel as float (0 to 1).
        /// </summary>
        [Category("Float color value"), Description("Get/Set alpha channel as float (0 to 1).")]
        public float AlphaF
        {
            get
            {
                return ((float)this.Alpha) / 255.0f;
            }
            set
            {
                this.Alpha = (byte)(value * 255.0f);
            }
        } // AlphaF

        /// <summary>
        /// Method read data from Xml file to structures of this data object.
        /// </summary>
        /// <param name="xmlTextReader">Xml stream to read.</param>
        public void ReadData(System.Xml.XmlTextReader xmlTextReader)
        {
            string[] tmp = xmlTextReader.ReadString().Trim().Split(' ', '\t');
            this.Red = Byte.Parse(tmp[0]);
            this.Green = Byte.Parse(tmp[1]);
            this.Blue = Byte.Parse(tmp[2]);
            this.Alpha = Byte.Parse(tmp[3]);
        } // ReadData()

        /// <summary>
        /// Method write data from structures of this data object to Xml file.
        /// </summary>
        /// <param name="xmlTextWriter">Xml stream to write.</param>
        public void WriteData(System.Xml.XmlTextWriter xmlTextWriter)
        {
            xmlTextWriter.WriteString(this.Red.ToString() + " " + this.Green.ToString() + " " +
                this.Blue.ToString() + " " + this.Alpha.ToString());
        } // WriteData()

        /// <summary>
        /// Deep copy of data object and its subelements.
        /// </summary>
        /// <returns>Same data, different memory space.</returns>
        public ColorRGBA DeepCopy()
        {
            return new ColorRGBA(this.Red, this.Green, this.Blue, this.Alpha);
        } // DeepCopy()

        // ===== Methods of the class Object ==============================================
        #region Overrided methods of the class Object

        /// <summary>
        /// Returns a String that represents the current Object.
        /// </summary>
        /// <returns>String that represents the current Object.</returns>
        override public string ToString()
        {
            return "ColorRGBA: Red = " + this.Red + ", Green = " + this.Green + ", Blue = " + this.Blue + ", Alpha = " + this.Alpha;
        } // ToString()

        /// <summary>
        /// Determines whether the specified Objects are equal.
        /// </summary>
        /// <param name="a">First Object.</param>
        /// <param name="b">Second Object.</param>
        /// <returns>True if objects are equal. Otherwise, false.</returns>
        public static new bool Equals(object a, object b)
        {
            if (a.GetType() != typeof(ColorRGBA) || b.GetType() != typeof(ColorRGBA))
                return false;

            ColorRGBA c1 = (ColorRGBA)a;
            ColorRGBA c2 = (ColorRGBA)b;

            return c1.Red == c2.Red
                && c1.Green == c2.Green
                && c1.Blue == c2.Blue
                && c1.Alpha == c2.Alpha;
        }

        /// <summary>
        /// Determines whether the specified Object is equal to the current Object.
        /// </summary>
        /// <param name="obj">Specified Object to compare with the current Object.</param>
        /// <returns>True if the specified Object is equal to the current Object. Otherwise, false.</returns>
        public override bool Equals(object obj)
        {
            return ColorRGBA.Equals(this, obj);
        }

        /// <summary>
        /// Returns a hash code for the current Object.
        /// </summary>
        /// <returns>A hash code for the current Object.</returns>
        public override int GetHashCode()
        {
            return this.Red.GetHashCode() ^ this.Green.GetHashCode() ^ this.Blue.GetHashCode() ^ this.Alpha.GetHashCode();
        }

        #endregion

        // ===== Conversions ==============================================================
        #region Conversion

        // ===== ColorRGBA <-> System.Drawing.Color =======
        /// <summary>
        /// Implicit conversion ColorRGBA -> Color
        /// </summary>
        /// <param name="c">ColorRGBA</param>
        /// <returns>Color</returns>
        public static implicit operator Color(ColorRGBA c)
        {
            return Color.FromArgb(c.Alpha, c.Red, c.Green, c.Blue);
        } // ColorRGBA -> Color

        /// <summary>
        /// Implicit conversion Color -> ColorRGBA
        /// </summary>
        /// <param name="c">Color</param>
        /// <returns>ColorRGBA</returns>
        public static implicit operator ColorRGBA(Color c)
        {
            return new ColorRGBA(c.R, c.G, c.B, c.A);
        } // Color -> ColorRGBA

        #endregion

        // ===== Operators ================================================================
        #region Operators

        /// <summary>
        /// The equality operator (==) returns true if the values of its operands are equal, false otherwise.
        /// </summary>
        /// <param name="a">First operand.</param>
        /// <param name="b">Second operand.</param>
        /// <returns>True if the values of operands are equal, false otherwise.</returns>
        public static bool operator ==(ColorRGBA a, ColorRGBA b)
        {
            return ColorRGBA.Equals(a, b);
        }

        /// <summary>
        /// The inequality operator (!=) returns false if its operands are equal, true otherwise.
        /// </summary>
        /// <param name="a">First operand.</param>
        /// <param name="b">Second operand.</param>
        /// <returns>False if the values of operands are equal, true otherwise.</returns>
        public static bool operator !=(ColorRGBA a, ColorRGBA b)
        {
            return !(a == b);
        }

        #endregion


        #region IColorVector Members

        ///// <summary>
        ///// Get number of elements of vector.
        ///// </summary>
        //[Browsable(false)]
        //public static int Length
        //{
        //    get { return 4; }
        //}

        /// <summary>
        /// Get/Set element of color vector.
        /// </summary>
        [Browsable(false)]
        public byte this[int index]
        {
            get
            {
                if (index == 0) return Red;
                else if (index == 1) return Green;
                else if (index == 2) return Blue;
                else if (index == 3) return Alpha;
                else throw new Exception("Color plane index is out of range.");
            }
            set
            {
                if (index == 0) Red = value;
                else if (index == 1) Green = value;
                else if (index == 2) Blue = value;
                else if (index == 3) Alpha = value;
                else throw new Exception("Color plane index is out of range.");
            }
        }

        #endregion
    } // ColorRGBA
} // namespace
