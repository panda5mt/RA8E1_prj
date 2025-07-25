/* generated configuration header file - do not edit */
#ifndef R_USB_BASIC_CFG_H_
#define R_USB_BASIC_CFG_H_
#ifdef __cplusplus
            extern "C" {
            #endif

            #ifndef NULL
            extern const uint16_t NULL[];
            #endif

            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HCDC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HCDC_USE
            #define USB_CFG_HCDC_ECM_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HHID_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HMSC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HVND_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HPRN_USE
            #endif
            #if ((1 != RA_NOT_DEFINED) || (RA_NOT_DEFINED != RA_NOT_DEFINED))
            #define USB_CFG_PCDC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PCDC2_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PHID_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PHID2_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PMSC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PPRN_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PVND_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HCDC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HMSC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HHID_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HPRN_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HUVC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_HAUD_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PCDC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PPRN_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PHID_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PMSC_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_PAUD_USE
            #endif
            #if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            #define USB_CFG_DFU_USE
            #endif
            #if ((RA_NOT_DEFINED != RA_NOT_DEFINED) || (RA_NOT_DEFINED != RA_NOT_DEFINED) || (RA_NOT_DEFINED != RA_NOT_DEFINED))
            #define USB_CFG_OTG_USE
            #endif
            #if (defined(USB_CFG_HCDC_USE) || defined(USB_CFG_HPRN_USE) || defined(USB_CFG_HMSC_USE) || defined(USB_CFG_HHID_USE) || defined(USB_CFG_HVND_USE) || defined(USB_CFG_HUVC_USE) || defined(USB_CFG_HAUD_USE))
            #define USB_CFG_HOST_MODE 1
            #else
            #define USB_CFG_HOST_MODE 0
            #endif

            #if (defined(USB_CFG_PCDC_USE) || defined(USB_CFG_PPRN_USE) || defined(USB_CFG_PMSC_USE) || defined(USB_CFG_PHID_USE) || defined(USB_CFG_PVND_USE) || defined(USB_CFG_PAUD_USE) || defined(USB_CFG_DFU_USE))
            #define USB_CFG_PERI_MODE 2
            #else
            #define USB_CFG_PERI_MODE 0
            #endif

            #define USB_CFG_MODE (USB_CFG_PERI_MODE | USB_CFG_HOST_MODE)

            #define USB_CFG_PARAM_CHECKING_ENABLE (BSP_CFG_PARAM_CHECKING_ENABLE)
            #define USB_CFG_CLKSEL (USB_CFG_24MHZ)
            #define USB_CFG_BUSWAIT (USB_CFG_BUSWAIT_7)
            #define USB_CFG_BC (USB_CFG_ENABLE)
            #define USB_CFG_VBUS (USB_CFG_HIGH)
            #define USB_CFG_DCP (USB_CFG_DISABLE)
            #define USB_CFG_CLASS_REQUEST (USB_CFG_ENABLE)
            #define USB_CFG_DBLB (USB_CFG_DBLBON)
            #define USB_CFG_CNTMD (USB_CFG_CNTMDOFF)
            #define USB_CFG_LDO_REGULATOR (USB_CFG_DISABLE)
            #define USB_CFG_TYPEC_FEATURE (RA_NOT_DEFINED)
            #define USB_CFG_DMA_DTC_DISABLED   (USB_CFG_ENABLE)
            #define USB_SRC_ADDRESS (NULL)
            #define USB_DEST_ADDRESS (NULL)
            #define USB_CFG_TPLCNT (1)
            #define USB_CFG_TPL USB_NOVENDOR, USB_NOPRODUCT
            #define USB_CFG_TPL_TABLE NULL
            #define USB_CFG_COMPLIANCE (USB_CFG_DISABLE)

            #ifdef __cplusplus
            }
            #endif
#endif /* R_USB_BASIC_CFG_H_ */
