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
        this.pictureBox = new System.Windows.Forms.PictureBox();
        this.statusStrip = new System.Windows.Forms.StatusStrip();
        this.toolStripStatusLabel = new System.Windows.Forms.ToolStripStatusLabel();
        ((System.ComponentModel.ISupportInitialize)this.pictureBox).BeginInit();
        this.statusStrip.SuspendLayout();
        this.SuspendLayout();
        // 
        // pictureBox
        // 
        this.pictureBox.Dock = System.Windows.Forms.DockStyle.Fill;
        this.pictureBox.Location = new System.Drawing.Point(0, 0);
        this.pictureBox.Name = "pictureBox";
        this.pictureBox.Size = new System.Drawing.Size(800, 428);
        this.pictureBox.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom;
        this.pictureBox.TabIndex = 0;
        this.pictureBox.TabStop = false;
        // 
        // statusStrip
        // 
        this.statusStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] { this.toolStripStatusLabel });
        this.statusStrip.Location = new System.Drawing.Point(0, 428);
        this.statusStrip.Name = "statusStrip";
        this.statusStrip.Size = new System.Drawing.Size(800, 22);
        this.statusStrip.TabIndex = 1;
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
        this.Controls.Add(this.pictureBox);
        this.Controls.Add(this.statusStrip);
        this.Name = "Form1";
        this.Text = "Real-time Video Stream (RA8E1)";
        ((System.ComponentModel.ISupportInitialize)this.pictureBox).EndInit();
        this.statusStrip.ResumeLayout(false);
        this.statusStrip.PerformLayout();
        this.ResumeLayout(false);
        this.PerformLayout();
    }

    #endregion

    private System.Windows.Forms.PictureBox pictureBox;
    private System.Windows.Forms.StatusStrip statusStrip;
    private System.Windows.Forms.ToolStripStatusLabel toolStripStatusLabel;
}
