/*
 * \brief  USB HID driver
 * \author Stefan Kalkowski
 * \date   2018-06-07
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <base/component.h>

#include <lx_kit/env.h>
#include <lx_kit/malloc.h>
#include <lx_kit/scheduler.h>
#include <lx_kit/timer.h>

#include <driver.h>
#include <lx_emul.h>

#include <lx_emul/extern_c_begin.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <lx_emul/extern_c_end.h>

extern "C" void usb_detect_interface_quirks(struct usb_device *udev);

void Driver::Device::register_device()
{
	if (udev) {
		Genode::error("device already registered!");
		return;
	}

	Usb::Device_descriptor dev_desc;
	Usb::Config_descriptor config_desc;
	usb.config_descriptor(&dev_desc, &config_desc);

	udev = (usb_device*) kzalloc(sizeof(usb_device), GFP_KERNEL);
	udev->bus = (usb_bus*) kzalloc(sizeof(usb_bus), GFP_KERNEL);
	udev->config = (usb_host_config*) kzalloc(sizeof(usb_host_config), GFP_KERNEL);
	udev->bus->bus_name = "usbbus";
	udev->bus->controller = (device*) (&usb);
	udev->bus_mA = 900; /* set to maximum USB3.0 */

	Genode::memcpy(&udev->descriptor,   &dev_desc,    sizeof(usb_device_descriptor));
	Genode::memcpy(&udev->config->desc, &config_desc, sizeof(usb_config_descriptor));
	udev->devnum = dev_desc.num;
	udev->speed  = (usb_device_speed) dev_desc.speed;
	udev->authorized = 1;

	int cfg = usb_get_configuration(udev);
	if (cfg < 0) {
		Genode::error("usb_get_configuration returned error ", cfg);
		return;
	}
	usb_detect_interface_quirks(udev);
	cfg = usb_choose_configuration(udev);
	usb_set_configuration(udev, cfg);

	for (int i = 0; i < udev->config->desc.bNumInterfaces; i++) {
		struct usb_interface      * iface = udev->config->interface[i];
		struct usb_host_interface * alt   = iface->cur_altsetting;

		for (int j = 0; j < alt->desc.bNumEndpoints; ++j) {
			struct usb_host_endpoint * ep = &alt->endpoint[j];
			int epnum  = usb_endpoint_num(&ep->desc);
			int is_out = usb_endpoint_dir_out(&ep->desc);
			if (is_out) udev->ep_out[epnum] = ep;
			else        udev->ep_in[epnum]  = ep;
		}

		struct usb_device_id   id;
		probe_interface(iface, &id);
	}
}


void Driver::Device::unregister_device()
{
	for (unsigned i = 0; i < USB_MAXINTERFACES; i++) {
		if (!udev->config->interface[i]) break;
		else remove_interface(udev->config->interface[i]);
	}
	kfree(udev->bus);
	kfree(udev->config);
	kfree(udev);
	udev = nullptr;
}


void Driver::Device::state_task_entry(void * arg)
{
	Device & dev = *reinterpret_cast<Device*>(arg);

	for (;;) {
		if (dev.usb.plugged() && !dev.udev)
			dev.register_device();

		if (!dev.usb.plugged() && dev.udev)
			dev.unregister_device();

		Lx::scheduler().current()->block_and_schedule();
	}
}


void Driver::Device::urb_task_entry(void * arg)
{
	Device & dev = *reinterpret_cast<Device*>(arg);

	for (;;) {
		while (dev.udev && dev.usb.source()->ack_avail()) {
			Usb::Packet_descriptor p = dev.usb.source()->get_acked_packet();
			if (p.completion) p.completion->complete(p);
			dev.usb.source()->release_packet(p);
		}

		Lx::scheduler().current()->block_and_schedule();
	}
}


Driver::Device::Device(Driver & driver, Label label)
: label(label),
  driver(driver),
  env(driver.env),
  alloc(driver.alloc),
  state_task(env.ep(), state_task_entry, reinterpret_cast<void*>(this),
             "usb_state", Lx::Task::PRIORITY_0, Lx::scheduler()),
  urb_task(env.ep(), urb_task_entry, reinterpret_cast<void*>(this),
             "usb_urb", Lx::Task::PRIORITY_0, Lx::scheduler())
{
	usb.tx_channel()->sigh_ack_avail(urb_task.handler);
	driver.devices.insert(&le);
}


Driver::Device::~Device()
{
	driver.devices.remove(&le);
	if (udev) unregister_device();
}


unsigned long Driver::screen_x    = 0;
unsigned long Driver::screen_y    = 0;
bool          Driver::multi_touch = false;
static Driver * driver = nullptr;


void Driver::main_task_entry(void * arg)
{
	driver = reinterpret_cast<Driver*>(arg);

	subsys_input_init();
	module_evdev_init();
	module_led_init();
	module_usbhid_init();
	module_hid_init();
	module_hid_generic_init();
	module_ch_driver_init();
	module_ms_driver_init();
	module_mt_driver_init();
	module_wacom_driver_init();

	bool use_report = false;

	try {
		Genode::Xml_node config_node = Lx_kit::env().config_rom().xml();
		use_report = config_node.attribute_value("use_report", false);
		config_node.attribute("width").value(&screen_x);
		config_node.attribute("height").value(&screen_y);
		multi_touch = config_node.attribute_value("multitouch", false);
	} catch(...) { }

	if (use_report)
		Genode::warning("use compatibility mode: ",
		                "will claim all HID devices from USB report");

	Genode::log("Configured HID screen with ", screen_x, "x", screen_y,
	            " (multitouch=", multi_touch ? "true" : "false", ")");

	driver->env.parent().announce(driver->ep.manage(driver->root));

	for (;;) {
		if (!use_report)
			static Device dev(*driver, Label(""));
		else
			driver->scan_report();
		Lx::scheduler().current()->block_and_schedule();
	}
}


void Driver::scan_report()
{
	if (!report_rom.constructed()) {
		report_rom.construct(env, "report");
		report_rom->sigh(main_task->handler);
	} else
		report_rom->update();

	devices.for_each([&] (Device & d) { d.updated = false; });

	try {
		Genode::Xml_node report_node = report_rom->xml();
		report_node.for_each_sub_node([&] (Genode::Xml_node & dev_node)
		{
			unsigned long c = 0;
			dev_node.attribute("class").value(&c);
			if (c != USB_CLASS_HID) return;

			Label label;
			dev_node.attribute("label").value(&label);

			bool found = false;

			devices.for_each([&] (Device & d) {
				if (d.label == label) d.updated = found = true; });

			if (!found)
				new (heap) Device(*this, label);
		});
	} catch(...) {
		Genode::error("Error parsing USB devices report");
	};

	devices.for_each([&] (Device & d) {
		if (!d.updated) destroy(heap, &d); });
}


void Driver::input_callback(Input_event type, unsigned code,
                            int ax, int ay, int rx, int ry)
{
	using namespace Input;

	auto submit = [&] (Event const &ev) { driver->session.submit(ev); };

	switch (type) {
	case EVENT_TYPE_PRESS:   submit(Press{Keycode(code)});    break;
	case EVENT_TYPE_RELEASE: submit(Release{Keycode(code)});  break;
	case EVENT_TYPE_MOTION:
		if (rx == 0 && ry == 0)
			submit(Absolute_motion{ax, ay});
		else
			submit(Relative_motion{rx, ry});
		break;
	case EVENT_TYPE_WHEEL:   submit(Wheel{rx, ry});           break;
	case EVENT_TYPE_TOUCH:
		{
			Touch_id const id { (int)code };

			if (rx == -1 && ry == -1)
				submit(Touch_release{id});
			else
				submit(Touch{id, (float)ax, (float)ay});
			break;
		}
	}
}


Driver::Driver(Genode::Env &env) : env(env)
{
	Genode::log("--- USB HID input driver ---");

	Lx_kit::construct_env(env);
	Lx::scheduler(&env);
	Lx::malloc_init(env, heap);
	Lx::timer(&env, &ep, &heap, &jiffies);

	main_task.construct(env.ep(), main_task_entry, reinterpret_cast<void*>(this),
	                    "main", Lx::Task::PRIORITY_0, Lx::scheduler());

	/* give all task a first kick before returning */
	Lx::scheduler().schedule();
}


void Component::construct(Genode::Env &env)
{
	env.exec_static_constructors();
	static Driver driver(env);
}
