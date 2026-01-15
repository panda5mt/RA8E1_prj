namespace UdpPhotoReceiver;

partial class Form1
{
    /// <summary>
    ///  Required designer variable.
    /// </summary>
    private System.ComponentModel.IContainer components = null;

    /// <summary>
    ///  Clean up any resources being used.
    /// </summary>
    /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
    protected override void Dispose(bool disposing)
    {
        if (disposing && (components != null))
        {
            components.Dispose();
        }
        base.Dispose(disposing);
    }

    #region Windows Form Designer generated code

    /// <summary>
    ///  Required method for Designer support - do not modify
    ///  the contents of this method with the code editor.
    /// </summary>
    private void InitializeComponent()
    {
        this.components = new System.ComponentModel.Container();
        this.toolStrip = new System.Windows.Forms.ToolStrip();
        this.toolStripLabelMode = new System.Windows.Forms.ToolStripLabel();
        this.toolStripComboMode = new System.Windows.Forms.ToolStripComboBox();
        this.toolStripSeparator1 = new System.Windows.Forms.ToolStripSeparator();
        this.toolStripLabelRange = new System.Windows.Forms.ToolStripLabel();
        this.numericHeatMin = new System.Windows.Forms.NumericUpDown();
        this.toolStripHostHeatMin = new System.Windows.Forms.ToolStripControlHost(this.numericHeatMin);
        this.toolStripLabelTo = new System.Windows.Forms.ToolStripLabel();
        this.numericHeatMax = new System.Windows.Forms.NumericUpDown();
        this.toolStripHostHeatMax = new System.Windows.Forms.ToolStripControlHost(this.numericHeatMax);
        this.pictureBox = new System.Windows.Forms.PictureBox();
        this.statusStrip = new System.Windows.Forms.StatusStrip();
        this.toolStripStatusLabel = new System.Windows.Forms.ToolStripStatusLabel();
        this.toolStrip.SuspendLayout();
        ((System.ComponentModel.ISupportInitialize)this.pictureBox).BeginInit();
        this.statusStrip.SuspendLayout();
        this.SuspendLayout();
        // 
        // toolStrip
        // 
        this.toolStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] { this.toolStripLabelMode, this.toolStripComboMode, this.toolStripSeparator1, this.toolStripLabelRange, this.toolStripHostHeatMin, this.toolStripLabelTo, this.toolStripHostHeatMax });
        this.toolStrip.Location = new System.Drawing.Point(0, 0);
        this.toolStrip.Name = "toolStrip";
        this.toolStrip.Size = new System.Drawing.Size(800, 25);
        this.toolStrip.TabIndex = 0;
        this.toolStrip.Text = "toolStrip";
        // 
        // toolStripLabelMode
        // 
        this.toolStripLabelMode.Name = "toolStripLabelMode";
        this.toolStripLabelMode.Size = new System.Drawing.Size(43, 22);
        this.toolStripLabelMode.Text = "Mode:";
        // 
        // toolStripComboMode
        // 
        this.toolStripComboMode.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
        this.toolStripComboMode.Name = "toolStripComboMode";
        this.toolStripComboMode.Size = new System.Drawing.Size(121, 25);

        // 
        // toolStripSeparator1
        // 
        this.toolStripSeparator1.Name = "toolStripSeparator1";
        this.toolStripSeparator1.Size = new System.Drawing.Size(6, 25);

        // 
        // toolStripLabelRange
        // 
        this.toolStripLabelRange.Name = "toolStripLabelRange";
        this.toolStripLabelRange.Size = new System.Drawing.Size(44, 22);
        this.toolStripLabelRange.Text = "Range:";

        // 
        // numericHeatMin
        // 
        this.numericHeatMin.Minimum = new decimal(new int[] { 0, 0, 0, 0 });
        this.numericHeatMin.Maximum = new decimal(new int[] { 255, 0, 0, 0 });
        this.numericHeatMin.Value = new decimal(new int[] { 0, 0, 0, 0 });
        this.numericHeatMin.Size = new System.Drawing.Size(56, 23);
        this.numericHeatMin.TextAlign = System.Windows.Forms.HorizontalAlignment.Right;

        // 
        // toolStripHostHeatMin
        // 
        this.toolStripHostHeatMin.Name = "toolStripHostHeatMin";
        this.toolStripHostHeatMin.Size = new System.Drawing.Size(60, 22);

        // 
        // toolStripLabelTo
        // 
        this.toolStripLabelTo.Name = "toolStripLabelTo";
        this.toolStripLabelTo.Size = new System.Drawing.Size(16, 22);
        this.toolStripLabelTo.Text = "..";

        // 
        // numericHeatMax
        // 
        this.numericHeatMax.Minimum = new decimal(new int[] { 0, 0, 0, 0 });
        this.numericHeatMax.Maximum = new decimal(new int[] { 255, 0, 0, 0 });
        this.numericHeatMax.Value = new decimal(new int[] { 255, 0, 0, 0 });
        this.numericHeatMax.Size = new System.Drawing.Size(56, 23);
        this.numericHeatMax.TextAlign = System.Windows.Forms.HorizontalAlignment.Right;

        // 
        // toolStripHostHeatMax
        // 
        this.toolStripHostHeatMax.Name = "toolStripHostHeatMax";
        this.toolStripHostHeatMax.Size = new System.Drawing.Size(60, 22);
        // 
        // pictureBox
        // 
        this.pictureBox.Dock = System.Windows.Forms.DockStyle.Fill;
        this.pictureBox.Location = new System.Drawing.Point(0, 25);
        this.pictureBox.Name = "pictureBox";
        this.pictureBox.Size = new System.Drawing.Size(800, 403);
        this.pictureBox.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom;
        this.pictureBox.TabIndex = 1;
        this.pictureBox.TabStop = false;
        // 
        // statusStrip
        // 
        this.statusStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] { this.toolStripStatusLabel });
        this.statusStrip.Location = new System.Drawing.Point(0, 428);
        this.statusStrip.Name = "statusStrip";
        this.statusStrip.Size = new System.Drawing.Size(800, 22);
        this.statusStrip.TabIndex = 2;
        this.statusStrip.Text = "statusStrip";
        // 
        // toolStripStatusLabel
        // 
        this.toolStripStatusLabel.Name = "toolStripStatusLabel";
        this.toolStripStatusLabel.Size = new System.Drawing.Size(169, 17);
        this.toolStripStatusLabel.Text = "Listening on UDP port 9000";
        // 
        // Form1
        // 
        this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
        this.ClientSize = new System.Drawing.Size(800, 450);
        this.Controls.Add(this.toolStrip);
        this.Controls.Add(this.pictureBox);
        this.Controls.Add(this.statusStrip);
        this.Name = "Form1";
        this.Text = "Real-time Video Stream (RA8E1)";
        this.toolStrip.ResumeLayout(false);
        this.toolStrip.PerformLayout();
        ((System.ComponentModel.ISupportInitialize)this.pictureBox).EndInit();
        this.statusStrip.ResumeLayout(false);
        this.statusStrip.PerformLayout();
        this.ResumeLayout(false);
        this.PerformLayout();
    }

    #endregion

    private System.Windows.Forms.ToolStrip toolStrip;
    private System.Windows.Forms.ToolStripLabel toolStripLabelMode;
    private System.Windows.Forms.ToolStripComboBox toolStripComboMode;
    private System.Windows.Forms.ToolStripSeparator toolStripSeparator1;
    private System.Windows.Forms.ToolStripLabel toolStripLabelRange;
    private System.Windows.Forms.NumericUpDown numericHeatMin;
    private System.Windows.Forms.ToolStripControlHost toolStripHostHeatMin;
    private System.Windows.Forms.ToolStripLabel toolStripLabelTo;
    private System.Windows.Forms.NumericUpDown numericHeatMax;
    private System.Windows.Forms.ToolStripControlHost toolStripHostHeatMax;
    private System.Windows.Forms.PictureBox pictureBox;
    private System.Windows.Forms.StatusStrip statusStrip;
    private System.Windows.Forms.ToolStripStatusLabel toolStripStatusLabel;
}
