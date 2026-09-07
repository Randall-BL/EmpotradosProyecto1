# Parametro de arranque del driver WiFi Broadcom.
#
# feature_disable=0x82000 apaga dos optimizaciones del brcmfmac que en la RPi4
# provocan desconexiones intermitentes bajo carga. El robot depende del WiFi para
# todo el control remoto, asi que se prioriza la estabilidad del enlace.

CMDLINE:append = " brcmfmac.feature_disable=0x82000"
