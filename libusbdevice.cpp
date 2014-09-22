#include "libusbdevice.h"
#include "libusbdeviceinfo.h"

LibUsbDevice::LibUsbDevice(QObject *parent) :
    QObject(parent)
{
    wayOfConnecting=false;
    enableEventThread = true;
    isDeviceConnected = false;
    isInitialiazed = false;
    deviceHandle = NULL;
    context = NULL;
    pcToUsbDeviceTransfer = NULL;
    usbDeviceToPcTransfer = NULL;
    dataAvailable = false;
    hasHotPlugSupport = false;
    deviceFound = NULL;
    connect(&serial,SIGNAL(newData(int)),this,SLOT(newDataAvailable(int)));
}
void LibUsbDevice::reset(){
    enableEventThread = true;
    isDeviceConnected = false;
    isInitialiazed = false;
    deviceHandle = NULL;
    context = NULL;
    pcToUsbDeviceTransfer = NULL;
    usbDeviceToPcTransfer = NULL;
    dataAvailable = false;
    hasHotPlugSupport = false;
    deviceFound = NULL;
}

extern "C" int LIBUSB_CALL hotplugAttachCallback (libusb_context *, libusb_device *, libusb_hotplug_event, void *ref)
{
    LibUsbDevice *cthis = static_cast<LibUsbDevice *>(ref);
    cthis->openDevice();
    qDebug()<<"Hotplug attach";
    if(cthis->isDeviceConnected)
        cthis->asyncBulkReadTransfer();
    return 0;
}

extern "C" int LIBUSB_CALL hotplugDetachCallback (libusb_context *, libusb_device *, libusb_hotplug_event, void *ref)
{
    qDebug()<<"Hotplug detach";
    LibUsbDevice *cthis = static_cast<LibUsbDevice *>(ref);
    if(cthis->isDeviceConnected)
    {
        if(cthis->usbDeviceToPcTransfer)
        {
            libusb_cancel_transfer(cthis->usbDeviceToPcTransfer);
        }
        if(cthis->pcToUsbDeviceTransfer)
        {
            libusb_cancel_transfer(cthis->pcToUsbDeviceTransfer);
        }
        libusb_release_interface(cthis->deviceHandle, INTERFACE_NUMBER);
        libusb_close(cthis->deviceHandle);
        cthis->isDeviceConnected = false;
    }
  return 0;
}

extern "C" void LIBUSB_CALL asyncBulkReadTransferCallback(struct libusb_transfer *transfer)
{
    LibUsbDevice *cthis = static_cast<LibUsbDevice *>(transfer->user_data);
    cthis->dataAvailable = true;
    if(cthis->enableEventThread)
    {
        cthis->dataLength = transfer->actual_length;
        libusb_submit_transfer(transfer);
    }

}

extern "C" void LIBUSB_CALL asyncBulkWriteTransferCallback(struct libusb_transfer *)
{

}

void LibUsbDevice::initializeDevice()
{
    if(wayOfConnecting){
        int status;
        status = libusb_init(&context); //initialize the library for the session we just declared
        if(status != 0)
        {
            cstatus = tr("Initialization Error!");
            qDebug()<<"Initialization Error! "<<status<<endl; //there was an error
            isInitialiazed = false;
            return;
        }
        isInitialiazed = true;
        if(libusb_has_capability (LIBUSB_CAP_HAS_HOTPLUG)&&0)
        {
           hasHotPlugSupport = true;
           //status = libusb_hotplug_register_callback (context, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, LIBUSB_HOTPLUG_ENUMERATE , VENDOR_ID,
                                                  //  PRODUCT_ID, CLASS_ID, hotplugAttachCallback, this, &hotplugHandle[0]);
           if (LIBUSB_SUCCESS != status)
           {
               hasHotPlugSupport = false;
           }

           //status = libusb_hotplug_register_callback (context, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, LIBUSB_HOTPLUG_ENUMERATE , VENDOR_ID,
                                                     // PRODUCT_ID, CLASS_ID, hotplugDetachCallback, this, &hotplugHandle[1]);
           if (LIBUSB_SUCCESS != status)
           {
               hasHotPlugSupport = false;
           }
           future = QtConcurrent::run (this,&LibUsbDevice::eventThread);
        }
        libusb_set_debug(context, LIBUSB_LOG_LEVEL_INFO); //set verbosity level to 3
    }else{
        isInitialiazed=true;        
    }
}

void LibUsbDevice::openDevice(QString nameOfPort)
{
    if(wayOfConnecting){
        size_t i = 0;
        int status;
        status = libusb_get_device_list(context, &devs);
        if (status < 0)
        {
            cstatus = tr("Cannot open device!");
            qDebug()<<"No USB Devices to List"<<endl;
            return;
        }
        while ((dev = devs[i++]) != NULL)
        {
            status = libusb_get_device_descriptor(dev, &deviceDesc);
            if (status < 0)
            {
                break;
            }
            if (deviceDesc.idVendor == VENDOR_ID && deviceDesc.idProduct == PRODUCT_ID)
            {
                status = libusb_open(dev, &deviceHandle);
                if (status)
                {
                    cstatus = tr("Cannot open device!");
                    qDebug()<<"Cannot open device--"<<i<<endl;
                    deviceHandle = NULL;
                }
                else
                {
                    qDebug()<<dev<<" Device Opened"<<endl;
                    break;
                }
            }
        }
        libusb_free_device_list(devs, 1);

        if(deviceHandle == NULL)
        {
            cstatus = tr("Cannot open device!");
            qDebug()<<"Cannot open device"<<endl;
            return;
        }

        if(libusb_kernel_driver_active(deviceHandle, 0) == 1)
        {
            qDebug()<<"Kernel Driver Active"<<endl;
            if(libusb_detach_kernel_driver(deviceHandle, 0) == 0) //detach it
                qDebug()<<"Kernel Driver Detached!"<<endl;
        }
        status = libusb_claim_interface(deviceHandle, INTERFACE_NUMBER);
        if(status != LIBUSB_SUCCESS) {
            qDebug()<<"Cannot Claim Interface"<<endl;
            libusb_close(deviceHandle);
            isDeviceConnected = false;
            return;
        }
        isDeviceConnected = true;
    }else{        
        serial.connectToPort(nameOfPort);
        isDeviceConnected=true;
    }
}

void LibUsbDevice::closeDevice()
{
    if(isDeviceConnected)
    {
        if(wayOfConnecting){
            if(usbDeviceToPcTransfer)
            {
                libusb_cancel_transfer(usbDeviceToPcTransfer);
            }
            if(pcToUsbDeviceTransfer)
            {
                libusb_cancel_transfer(pcToUsbDeviceTransfer);
            }
            enableEventThread = false;
            if(hasHotPlugSupport)
            {
                //            libusb_hotplug_deregister_callback(context, hotplugHandle[0]);
                //            libusb_hotplug_deregister_callback(context, hotplugHandle[1]);
            }
            future.waitForFinished();
            saveDeviceSettings();
            libusb_release_interface(deviceHandle, INTERFACE_NUMBER);
            libusb_close(deviceHandle);
            libusb_exit(context);
            isDeviceConnected=false;
        }else{
            serial.sendData=false;
            serial.finish=true;
            future.waitForFinished();
            serial.write("p");
            enableEventThread = false;
            saveDeviceSettings();
            serial.close();
            isDeviceConnected=false;
        }
    }
    else if(isInitialiazed)
    {
        if(wayOfConnecting){
            enableEventThread = false;
            if(hasHotPlugSupport)
            {
                //            libusb_hotplug_deregister_callback(context, hotplugHandle[0]);
                //            libusb_hotplug_deregister_callback(context, hotplugHandle[1]);
            }
            future.waitForFinished();
            libusb_exit(context);
        }else{
            enableEventThread = false;
            serial.finish=true;
            future.waitForFinished();
            serial.sendData=false;
            serial.write("p");
            serial.close();
        }
    }

}

void LibUsbDevice::eventThread()
{
    if(wayOfConnecting){
        while (enableEventThread)
        {
            libusb_handle_events(context);
        }
    }
    return;
}

bool LibUsbDevice::controlReadTransfer(uint8_t command, uint16_t value , uint16_t index)
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return false;
    }
    if(wayOfConnecting){
        int bytesRead;
        bytesRead = libusb_control_transfer(deviceHandle,0xC0,command,value,index,inBuffer,LEN_CONTROL_BUFFER,1000);
        if (bytesRead >= 0)
        {
            if(bytesRead<LEN_CONTROL_BUFFER)
                inBuffer[bytesRead] = '\0';
            return true;
        }
        else
        {
            cstatus = tr("Read Error!");
            qDebug()<<"read error: "<<bytesRead;
            return false;
        }
    }else{
        serial.sendData=false;
        serial.serial->clear();
        serial.write("p");

        /*serial.write(QString(command));
        serial.write(QString(qToLittleEndian(index)));
        serial.write(QString(qToLittleEndian(value)));*/
            QByteArray ba;
            ba[0]=qToLittleEndian(command);
            ba[1]=qToLittleEndian(index);
            ba[2]=qToLittleEndian(index >> 8);
            ba[3]=qToLittleEndian(value);
            ba[4]=qToLittleEndian(value >> 8);

            serial.writeByteArray(ba);

        while(serial.serial->bytesAvailable()<44){
            qDebug()<<serial.serial->bytesAvailable();
            serial.serial->waitForReadyRead(1000);
        }
        char tmp[LEN_CONTROL_BUFFER];
        int size=serial.serial->read(tmp,LEN_CONTROL_BUFFER);
        for(int i=0;i<size;i++){
            inBuffer[i]=tmp[i];
        }
        QTimer::singleShot(1000,this,SLOT(turnOnAutoMode()));
        if (size >= 0)
        {
            if(size<LEN_CONTROL_BUFFER)
                inBuffer[size] = '\0';
            return true;
        }
        else
        {
            cstatus = tr("Read Error!");
            qDebug()<<"read error: "<<size;
            return false;
        }
    }
    return false;
}

void LibUsbDevice::asyncBulkReadTransfer()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting){
        int status;
        usbDeviceToPcTransfer = libusb_alloc_transfer(0);
        libusb_fill_bulk_transfer( usbDeviceToPcTransfer, deviceHandle, USB_ENDPOINT_IN,
                    chData, LEN_BULK_IN_BUFFER,asyncBulkReadTransferCallback,this,0);
        status = libusb_submit_transfer(usbDeviceToPcTransfer);
        if(status!=0)
        {
            cstatus = tr("Bulk read transfer failed!");
            qDebug()<<"Transfer Failed! "<<status;
            return;
        }
        if(!future.isRunning())
            future = QtConcurrent::run (this,&LibUsbDevice::eventThread);
    }else{
        serial.wsk=chData;
        serial.finish=false;
        if(!future.isRunning())
            future = QtConcurrent::run(&serial,&SerialPortConnection::run);
    }
}

void LibUsbDevice::controlWriteTransfer(uint16_t index, uint8_t value)
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'b',value,index,NULL,0,1000);
    else{
        QByteArray ba;
        ba[0]='b';
        ba[1]=index;
        ba[2]=value;
        serial.writeByteArray(ba);
    }
}

void LibUsbDevice::saveDeviceSettings()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'d',0,0,NULL,0,1000);
    else
        serial.write("d");
}

void LibUsbDevice::awgBulkWriteTransfer()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    int actual;
    if(wayOfConnecting)
        libusb_bulk_transfer(deviceHandle,USB_ENDPOINT_OUT,awgBuffer,256,&actual,1000);
    else{
        serial.sendData=false;
        while(serial.serial->bytesAvailable()!=256){
            serial.serial->waitForReadyRead(1000);
        }
        char tmp[256];
        int size=serial.serial->read(tmp,256);
        for(int i=0;i<size;i++){
            awgBuffer[i]=tmp[i];
        }
        serial.sendData=true;
    }
}

QString LibUsbDevice::requestFirmwareVersion()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return NULL;
    }    
        int bytesRead=0;
        unsigned char buffer[4];
        if(wayOfConnecting){
            bytesRead = libusb_control_transfer(deviceHandle,0xC0,'a',0,0,buffer,4,1000);
        }else{
            serial.serial->clear();
            serial.write("a");
            char tmp[4];
            while(serial.serial->bytesAvailable()!=4){
                serial.serial->waitForReadyRead(1000);
            }
            bytesRead=serial.serial->read(tmp,4);
            for(int i=0;i<bytesRead;i++){
                buffer[i]=tmp[i];
            }
            qDebug()<<"VERSION: "<<getStringFromUnsignedChar(buffer,4);
        }
        if (bytesRead > 0)
        {
            return getStringFromUnsignedChar(buffer,4);
        }
        else
        {
            cstatus = tr("Read Error!");
            qDebug()<<"read error: "<<bytesRead;
            return NULL;
        }
}

void LibUsbDevice::stopScope()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'f',0,0,NULL,0,1000);
    else
        serial.write("f");
}
void LibUsbDevice::startScope()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'g',0,0,NULL,0,1000);
    else
        serial.write("g");
}

void LibUsbDevice::restoreSettings()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'k',0,0,NULL,0,1000);
    else
        serial.write("k");
}

void LibUsbDevice::forceTrigger()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'h',0,0,NULL,0,1000);
    else
        serial.write("h");
}

void LibUsbDevice::autoSetup()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'i',0,0,NULL,0,1000);
    else
        serial.write("i");
}

void LibUsbDevice::saveAWG()
{
    if(!isDeviceConnected)
    {
        qDebug()<<"Device not connected";
        return;
    }
    if(wayOfConnecting)
        libusb_control_transfer(deviceHandle,0xC0,'e',0,0,NULL,0,1000);
    else
        serial.write("e");
}

QString LibUsbDevice::getStringFromUnsignedChar( unsigned char *buffer,int length )
{
    QString result;
    for( int i = 0; i < length; i++ )
    {
        result.append(buffer[i]);
    }
    return result;
}
void LibUsbDevice::newDataAvailable(int size){
    dataAvailable=true;
    dataLength=size;
}
void LibUsbDevice::turnOnAutoMode(){
    serial.serial->clear();
    serial.sendData=true;
    serial.write("q");
}
