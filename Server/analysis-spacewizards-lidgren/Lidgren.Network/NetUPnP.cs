using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Xml;

namespace Lidgren.Network;

public class NetUPnP
{
	private struct UpnpCandidate
	{
		public string Url;

		public string ServiceName;
	}

	private const int c_discoveryTimeOutMillis = 1000;

	private string? m_serviceUrl;

	private string m_serviceName = "";

	private readonly NetPeer m_peer;

	private readonly ManualResetEvent m_discoveryComplete = new ManualResetEvent(initialState: false);

	internal double m_discoveryResponseDeadline;

	private UPnPStatus m_status;

	private readonly List<UpnpCandidate> m_candidates = new List<UpnpCandidate>();

	public UPnPStatus Status => m_status;

	public NetUPnP(NetPeer peer)
	{
		m_peer = peer;
		m_discoveryResponseDeadline = double.PositiveInfinity;
	}

	internal void Discover(NetPeer peer)
	{
		string s = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nST:upnp:rootdevice\r\nMAN:\"ssdp:discover\"\r\nMX:3\r\n\r\n";
		m_discoveryResponseDeadline = NetTime.Now + 6.0;
		m_status = UPnPStatus.Discovering;
		byte[] bytes = Encoding.UTF8.GetBytes(s);
		IPAddress broadcastAddress = NetUtility.GetBroadcastAddress();
		if (broadcastAddress == null)
		{
			throw new InvalidOperationException("Unable to determine broadcast address");
		}
		peer.Socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: true);
		peer.RawSend(bytes, 0, bytes.Length, new IPEndPoint(broadcastAddress, 1900));
		peer.Socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: false);
	}

	internal void CheckForDiscoveryTimeout()
	{
		if (m_status == UPnPStatus.Discovering && !(NetTime.Now < m_discoveryResponseDeadline))
		{
			if (m_candidates.Count > 0)
			{
				UpnpCandidate upnpCandidate = SelectUpnpCandidate();
				m_serviceUrl = upnpCandidate.Url;
				m_serviceName = upnpCandidate.ServiceName;
				m_status = UPnPStatus.Available;
				m_discoveryComplete.Set();
			}
			else
			{
				m_status = UPnPStatus.NotAvailable;
			}
		}
	}

	private UpnpCandidate SelectUpnpCandidate()
	{
		return m_candidates.OrderByDescending((UpnpCandidate c) => CandidateScore(in c)).First();
		int CandidateScore(in UpnpCandidate candidate)
		{
			string connectionStatus = GetConnectionStatus(candidate.Url, candidate.ServiceName);
			if (connectionStatus != "Connected" && connectionStatus != "Up")
			{
				return 1;
			}
			IPAddress externalIP = GetExternalIP(candidate.Url, candidate.ServiceName);
			if (externalIP == null || NetReservedAddress.IsAddressReserved(externalIP))
			{
				return 2;
			}
			return 3;
		}
	}

	internal void ExtractServiceUrl(string resp)
	{
		try
		{
			using HttpClient httpClient = new HttpClient();
			HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, resp);
			XmlDocument xmlDocument = SyncSend(httpClient, request);
			XmlNamespaceManager xmlNamespaceManager = new XmlNamespaceManager(xmlDocument.NameTable);
			xmlNamespaceManager.AddNamespace("tns", "urn:schemas-upnp-org:device-1-0");
			XmlNode? xmlNode = xmlDocument.SelectSingleNode("//tns:device/tns:deviceType/text()", xmlNamespaceManager);
			if (xmlNode == null || xmlNode.Value?.Contains("InternetGatewayDevice") != true)
			{
				m_peer.LogWarning("Failed to resolve UPnP: couldn't find InternetGatewayDevice deviceType in the response");
				return;
			}
			string text = "WANIPConnection";
			XmlNode xmlNode2 = xmlDocument.SelectSingleNode("//tns:service[tns:serviceType=\"urn:schemas-upnp-org:service:" + text + ":1\"]/tns:controlURL/text()", xmlNamespaceManager);
			if (xmlNode2 == null)
			{
				text = "WANPPPConnection";
				xmlNode2 = xmlDocument.SelectSingleNode("//tns:service[tns:serviceType=\"urn:schemas-upnp-org:service:" + text + ":1\"]/tns:controlURL/text()", xmlNamespaceManager);
			}
			if (xmlNode2 == null || xmlNode2.Value == null)
			{
				m_peer.LogWarning("Failed to resolve UPnP: there aren't any supported services (WANIPConnection or WANPPPConnection) in the response");
				return;
			}
			string url = CombineUrls(resp, xmlNode2.Value);
			m_candidates.Add(new UpnpCandidate
			{
				ServiceName = text,
				Url = url
			});
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

	public bool ForwardPort(int externalPort, string description, int internalPort = 0, string proto = "UDP")
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
		if (internalPort == 0)
		{
			internalPort = externalPort;
		}
		try
		{
			SOAPRequest(m_serviceUrl, m_serviceName, "<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:" + m_serviceName + ":1\"><NewRemoteHost></NewRemoteHost><NewExternalPort>" + externalPort + "</NewExternalPort><NewProtocol>" + proto + "</NewProtocol><NewInternalPort>" + internalPort + "</NewInternalPort><NewInternalClient>" + myAddress.ToString() + "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>" + description + "</NewPortMappingDescription><NewLeaseDuration>0</NewLeaseDuration></u:AddPortMapping>", "AddPortMapping");
			NetUtility.Sleep(50);
		}
		catch (Exception ex)
		{
			NetPeer peer = m_peer;
			bool handlerIsValid;
			NetPeer.NetWarningLogInterpolatedStringHandler text = new NetPeer.NetWarningLogInterpolatedStringHandler(26, 1, peer, out handlerIsValid);
			if (handlerIsValid)
			{
				text.AppendLiteral("UPnP port forward failed: ");
				text.AppendFormatted(ex.Message);
			}
			peer.LogWarning(text);
			handlerIsValid = false;
			return handlerIsValid;
		}
		return true;
	}

	public bool DeleteForwardingRule(int externalPort, string proto = "UDP")
	{
		if (!CheckAvailability())
		{
			return false;
		}
		try
		{
			SOAPRequest(m_serviceUrl, m_serviceName, "<u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:" + m_serviceName + ":1\"><NewRemoteHost></NewRemoteHost><NewExternalPort>" + externalPort + "</NewExternalPort><NewProtocol>" + proto + "</NewProtocol></u:DeletePortMapping>", "DeletePortMapping");
			return true;
		}
		catch (Exception ex)
		{
			NetPeer peer = m_peer;
			bool handlerIsValid;
			NetPeer.NetWarningLogInterpolatedStringHandler text = new NetPeer.NetWarningLogInterpolatedStringHandler(36, 1, peer, out handlerIsValid);
			if (handlerIsValid)
			{
				text.AppendLiteral("UPnP delete forwarding rule failed: ");
				text.AppendFormatted(ex.Message);
			}
			peer.LogWarning(text);
			return false;
		}
	}

	public IPAddress? GetExternalIP()
	{
		if (!CheckAvailability())
		{
			return null;
		}
		return GetExternalIP(m_serviceUrl, m_serviceName);
	}

	private IPAddress? GetExternalIP(string url, string name)
	{
		try
		{
			XmlDocument xmlDocument = SOAPRequest(url, name, "<u:GetExternalIPAddress xmlns:u=\"urn:schemas-upnp-org:service:" + name + ":1\"></u:GetExternalIPAddress>", "GetExternalIPAddress");
			XmlNamespaceManager xmlNamespaceManager = new XmlNamespaceManager(xmlDocument.NameTable);
			xmlNamespaceManager.AddNamespace("tns", "urn:schemas-upnp-org:device-1-0");
			string text = xmlDocument.SelectSingleNode("//NewExternalIPAddress/text()", xmlNamespaceManager)?.Value;
			if (text == null)
			{
				m_peer.LogWarning("Failed to read external IP from the UPnP response");
				return null;
			}
			return IPAddress.Parse(text);
		}
		catch (Exception ex)
		{
			NetPeer peer = m_peer;
			bool handlerIsValid;
			NetPeer.NetWarningLogInterpolatedStringHandler text2 = new NetPeer.NetWarningLogInterpolatedStringHandler(27, 1, peer, out handlerIsValid);
			if (handlerIsValid)
			{
				text2.AppendLiteral("Failed to get external IP: ");
				text2.AppendFormatted(ex.Message);
			}
			peer.LogWarning(text2);
			return null;
		}
	}

	private string? GetConnectionStatus(string url, string name)
	{
		try
		{
			XmlDocument xmlDocument = SOAPRequest(url, name, "<u:GetStatusInfo xmlns:u=\"urn:schemas-upnp-org:service:" + name + ":1\" />", "GetStatusInfo");
			XmlNamespaceManager xmlNamespaceManager = new XmlNamespaceManager(xmlDocument.NameTable);
			xmlNamespaceManager.AddNamespace("tns", "urn:schemas-upnp-org:device-1-0");
			string? obj = xmlDocument.SelectSingleNode("//NewConnectionStatus/text()", xmlNamespaceManager)?.Value;
			if (obj == null)
			{
				m_peer.LogWarning("Failed to read connection status from the UPnP response");
			}
			return obj;
		}
		catch (Exception ex)
		{
			NetPeer peer = m_peer;
			bool handlerIsValid;
			NetPeer.NetWarningLogInterpolatedStringHandler text = new NetPeer.NetWarningLogInterpolatedStringHandler(33, 1, peer, out handlerIsValid);
			if (handlerIsValid)
			{
				text.AppendLiteral("Failed to get connection status: ");
				text.AppendFormatted(ex.Message);
			}
			peer.LogWarning(text);
			return null;
		}
	}

	private XmlDocument SOAPRequest(string url, string serviceName, string soap, string function)
	{
		string content = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>" + soap + "</s:Body></s:Envelope>";
		using HttpRequestMessage httpRequestMessage = new HttpRequestMessage(HttpMethod.Post, url);
		httpRequestMessage.Headers.Add("SOAPACTION", $"\"urn:schemas-upnp-org:service:{serviceName}:1#{function}\"");
		using StringContent content2 = new StringContent(content, null, "text/xml");
		httpRequestMessage.Content = content2;
		using HttpClient httpClient = new HttpClient();
		return SyncSend(httpClient, httpRequestMessage);
	}

	private static XmlDocument SyncSend(HttpClient httpClient, HttpRequestMessage request)
	{
		XmlDocument xmlDocument = new XmlDocument();
		using HttpResponseMessage httpResponseMessage = httpClient.Send(request);
		httpResponseMessage.EnsureSuccessStatusCode();
		xmlDocument.Load(httpResponseMessage.Content.ReadAsStream());
		return xmlDocument;
	}
}
