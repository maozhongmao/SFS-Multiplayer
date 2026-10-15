using System;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Xml;

namespace Lidgren.Network;

public class NetUPnP
{
	private const int c_discoveryTimeOutMillis = 1000;

	private string m_serviceUrl;

	private string m_serviceName = "";

	private NetPeer m_peer;

	private ManualResetEvent m_discoveryComplete = new ManualResetEvent(initialState: false);

	internal double m_discoveryResponseDeadline;

	private UPnPStatus m_status;

	public UPnPStatus Status => m_status;

	public NetUPnP(NetPeer peer)
	{
		m_peer = peer;
		m_discoveryResponseDeadline = double.MinValue;
	}

	internal void Discover(NetPeer peer)
	{
		string s = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nST:upnp:rootdevice\r\nMAN:\"ssdp:discover\"\r\nMX:3\r\n\r\n";
		m_discoveryResponseDeadline = NetTime.Now + 6.0;
		m_status = UPnPStatus.Discovering;
		byte[] bytes = Encoding.UTF8.GetBytes(s);
		peer.Socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: true);
		peer.RawSend(bytes, 0, bytes.Length, new IPEndPoint(NetUtility.GetBroadcastAddress(), 1900));
		peer.Socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: false);
	}

	internal void CheckForDiscoveryTimeout()
	{
		if (m_status == UPnPStatus.Discovering && !(NetTime.Now < m_discoveryResponseDeadline))
		{
			m_status = UPnPStatus.NotAvailable;
		}
	}

	internal void ExtractServiceUrl(string resp)
	{
		try
		{
			XmlDocument xmlDocument = new XmlDocument();
			using (WebResponse webResponse = WebRequest.Create(resp).GetResponse())
			{
				xmlDocument.Load(webResponse.GetResponseStream());
			}
			XmlNamespaceManager xmlNamespaceManager = new XmlNamespaceManager(xmlDocument.NameTable);
			xmlNamespaceManager.AddNamespace("tns", "urn:schemas-upnp-org:device-1-0");
			XmlNode xmlNode = xmlDocument.SelectSingleNode("//tns:device/tns:deviceType/text()", xmlNamespaceManager);
			if (!xmlNode.Value.Contains("InternetGatewayDevice"))
			{
				return;
			}
			m_serviceName = "WANIPConnection";
			XmlNode xmlNode2 = xmlDocument.SelectSingleNode("//tns:service[tns:serviceType=\"urn:schemas-upnp-org:service:" + m_serviceName + ":1\"]/tns:controlURL/text()", xmlNamespaceManager);
			if (xmlNode2 == null)
			{
				m_serviceName = "WANPPPConnection";
				xmlNode2 = xmlDocument.SelectSingleNode("//tns:service[tns:serviceType=\"urn:schemas-upnp-org:service:" + m_serviceName + ":1\"]/tns:controlURL/text()", xmlNamespaceManager);
				if (xmlNode2 == null)
				{
					return;
				}
			}
			m_serviceUrl = CombineUrls(resp, xmlNode2.Value);
			m_status = UPnPStatus.Available;
			m_discoveryComplete.Set();
		}
		catch
		{
		}
	}

	private static string CombineUrls(string gatewayURL, string subURL)
	{
		if (subURL.Contains("http:") || subURL.Contains("."))
		{
			return subURL;
		}
		gatewayURL = gatewayURL.Replace("http://", "");
		int num = gatewayURL.IndexOf("/");
		if (num != -1)
		{
			gatewayURL = gatewayURL.Substring(0, num);
		}
		return "http://" + gatewayURL + subURL;
	}

	private bool CheckAvailability()
	{
		switch (m_status)
		{
		case UPnPStatus.NotAvailable:
			return false;
		case UPnPStatus.Available:
			return true;
		case UPnPStatus.Discovering:
			if (m_discoveryComplete.WaitOne(1000))
			{
				return true;
			}
			if (NetTime.Now > m_discoveryResponseDeadline)
			{
				m_status = UPnPStatus.NotAvailable;
			}
			return false;
		default:
			return false;
		}
	}

	public bool ForwardPort(int port, string description)
	{
		if (!CheckAvailability())
		{
			return false;
		}
		IPAddress mask;
		IPAddress myAddress = NetUtility.GetMyAddress(out mask);
		if (myAddress == null)
		{
			return false;
		}
		try
		{
			SOAPRequest(m_serviceUrl, "<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:" + m_serviceName + ":1\"><NewRemoteHost></NewRemoteHost><NewExternalPort>" + port + "</NewExternalPort><NewProtocol>" + ProtocolType.Udp.ToString().ToUpper(CultureInfo.InvariantCulture) + "</NewProtocol><NewInternalPort>" + port + "</NewInternalPort><NewInternalClient>" + myAddress.ToString() + "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>" + description + "</NewPortMappingDescription><NewLeaseDuration>0</NewLeaseDuration></u:AddPortMapping>", "AddPortMapping");
			NetUtility.Sleep(50);
		}
		catch (Exception ex)
		{
			m_peer.LogWarning("UPnP port forward failed: " + ex.Message);
			return false;
		}
		return true;
	}

	public bool DeleteForwardingRule(int port)
	{
		if (!CheckAvailability())
		{
			return false;
		}
		try
		{
			SOAPRequest(m_serviceUrl, "<u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:" + m_serviceName + ":1\"><NewRemoteHost></NewRemoteHost><NewExternalPort>" + port + "</NewExternalPort><NewProtocol>" + ProtocolType.Udp.ToString().ToUpper(CultureInfo.InvariantCulture) + "</NewProtocol></u:DeletePortMapping>", "DeletePortMapping");
			return true;
		}
		catch (Exception ex)
		{
			m_peer.LogWarning("UPnP delete forwarding rule failed: " + ex.Message);
			return false;
		}
	}

	public IPAddress GetExternalIP()
	{
		if (!CheckAvailability())
		{
			return null;
		}
		try
		{
			XmlDocument xmlDocument = SOAPRequest(m_serviceUrl, "<u:GetExternalIPAddress xmlns:u=\"urn:schemas-upnp-org:service:" + m_serviceName + ":1\"></u:GetExternalIPAddress>", "GetExternalIPAddress");
			XmlNamespaceManager xmlNamespaceManager = new XmlNamespaceManager(xmlDocument.NameTable);
			xmlNamespaceManager.AddNamespace("tns", "urn:schemas-upnp-org:device-1-0");
			string value = xmlDocument.SelectSingleNode("//NewExternalIPAddress/text()", xmlNamespaceManager).Value;
			return IPAddress.Parse(value);
		}
		catch (Exception ex)
		{
			m_peer.LogWarning("Failed to get external IP: " + ex.Message);
			return null;
		}
	}

	private XmlDocument SOAPRequest(string url, string soap, string function)
	{
		string s = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>" + soap + "</s:Body></s:Envelope>";
		WebRequest webRequest = WebRequest.Create(url);
		webRequest.Method = "POST";
		byte[] bytes = Encoding.UTF8.GetBytes(s);
		webRequest.Headers.Add("SOAPACTION", "\"urn:schemas-upnp-org:service:" + m_serviceName + ":1#" + function + "\"");
		webRequest.ContentType = "text/xml; charset=\"utf-8\"";
		webRequest.ContentLength = bytes.Length;
		webRequest.GetRequestStream().Write(bytes, 0, bytes.Length);
		using WebResponse webResponse = webRequest.GetResponse();
		XmlDocument xmlDocument = new XmlDocument();
		Stream responseStream = webResponse.GetResponseStream();
		xmlDocument.Load(responseStream);
		return xmlDocument;
	}
}
