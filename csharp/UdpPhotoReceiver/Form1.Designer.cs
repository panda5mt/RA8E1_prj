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
        this.toolStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] { this.toolStripLabelMode, this.toolStripComboMode });
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
    private System.Windows.Forms.PictureBox pictureBox;
    private System.Windows.Forms.StatusStrip statusStrip;
    private System.Windows.Forms.ToolStripStatusLabel toolStripStatusLabel;
}
