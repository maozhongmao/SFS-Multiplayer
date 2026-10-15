using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;

namespace Lidgren.Network;

[DebuggerDisplay("Count={Count} Capacity={Capacity}")]
public sealed class NetQueue<T>
{
	private T[] m_items;

	private readonly ReaderWriterLockSlim m_lock = new ReaderWriterLockSlim();

	private int m_size;

	private int m_head;

	public int Count
	{
		get
		{
			m_lock.EnterReadLock();
			int size = m_size;
			m_lock.ExitReadLock();
			return size;
		}
	}

	public int Capacity
	{
		get
		{
			m_lock.EnterReadLock();
			int result = m_items.Length;
			m_lock.ExitReadLock();
			return result;
		}
	}

	public NetQueue(int initialCapacity)
	{
		m_items = new T[initialCapacity];
	}

	public void Enqueue(T item)
	{
		m_lock.EnterWriteLock();
		try
		{
			if (m_size == m_items.Length)
			{
				SetCapacity(m_items.Length + 8);
			}
			int num = (m_head + m_size) % m_items.Length;
			m_items[num] = item;
			m_size++;
		}
		finally
		{
			m_lock.ExitWriteLock();
		}
	}

	public void Enqueue(IEnumerable<T> items)
	{
		m_lock.EnterWriteLock();
		try
		{
			foreach (T item in items)
			{
				if (m_size == m_items.Length)
				{
					SetCapacity(m_items.Length + 8);
				}
				int num = (m_head + m_size) % m_items.Length;
				m_items[num] = item;
				m_size++;
			}
		}
		finally
		{
			m_lock.ExitWriteLock();
		}
	}

	public void EnqueueFirst(T item)
	{
		m_lock.EnterWriteLock();
		try
		{
			if (m_size >= m_items.Length)
			{
				SetCapacity(m_items.Length + 8);
			}
			m_head--;
			if (m_head < 0)
			{
				m_head = m_items.Length - 1;
			}
			m_items[m_head] = item;
			m_size++;
		}
		finally
		{
			m_lock.ExitWriteLock();
		}
	}

	private void SetCapacity(int newCapacity)
	{
		if (m_size == 0 && m_size == 0)
		{
			m_items = new T[newCapacity];
			m_head = 0;
			return;
		}
		T[] array = new T[newCapacity];
		if (m_head + m_size - 1 < m_items.Length)
		{
			Array.Copy(m_items, m_head, array, 0, m_size);
		}
		else
		{
			Array.Copy(m_items, m_head, array, 0, m_items.Length - m_head);
			Array.Copy(m_items, 0, array, m_items.Length - m_head, m_size - (m_items.Length - m_head));
		}
		m_items = array;
		m_head = 0;
	}

	public bool TryDequeue(out T item)
	{
		if (m_size == 0)
		{
			item = default(T);
			return false;
		}
		m_lock.EnterWriteLock();
		try
		{
			if (m_size == 0)
			{
				item = default(T);
				return false;
			}
			item = m_items[m_head];
			m_items[m_head] = default(T);
			m_head = (m_head + 1) % m_items.Length;
			m_size--;
			return true;
		}
		catch
		{
			item = default(T);
			return false;
		}
		finally
		{
			m_lock.ExitWriteLock();
		}
	}

	public int TryDrain(IList<T> addTo)
	{
		if (m_size == 0)
		{
			return 0;
		}
		m_lock.EnterWriteLock();
		try
		{
			int size = m_size;
			while (m_size > 0)
			{
				T item = m_items[m_head];
				addTo.Add(item);
				m_items[m_head] = default(T);
				m_head = (m_head + 1) % m_items.Length;
				m_size--;
			}
			return size;
		}
		finally
		{
			m_lock.ExitWriteLock();
		}
	}

	public T TryPeek(int offset)
	{
		if (m_size == 0)
		{
			return default(T);
		}
		m_lock.EnterReadLock();
		try
		{
			if (m_size == 0)
			{
				return default(T);
			}
			return m_items[(m_head + offset) % m_items.Length];
		}
		finally
		{
			m_lock.ExitReadLock();
		}
	}

	public bool Contains(T item)
	{
		m_lock.EnterReadLock();
		try
		{
			int num = m_head;
			for (int i = 0; i < m_size; i++)
			{
				if (m_items[num] == null)
				{
					if (item == null)
					{
						return true;
					}
				}
				else
				{
					ref readonly T reference = ref m_items[num];
					object obj = item;
					if (reference.Equals(obj))
					{
						return true;
					}
				}
				num = (num + 1) % m_items.Length;
			}
			return false;
		}
		finally
		{
			m_lock.ExitReadLock();
		}
	}

	public T[] ToArray()
	{
		m_lock.EnterReadLock();
		try
		{
			T[] array = new T[m_size];
			int num = m_head;
			for (int i = 0; i < m_size; i++)
			{
				array[i] = m_items[num++];
				if (num >= m_items.Length)
				{
					num = 0;
				}
			}
			return array;
		}
		finally
		{
			m_lock.ExitReadLock();
		}
	}

	public void Clear()
	{
		m_lock.EnterWriteLock();
		try
		{
			for (int i = 0; i < m_items.Length; i++)
			{
				m_items[i] = default(T);
			}
			m_head = 0;
			m_size = 0;
		}
		finally
		{
			m_lock.ExitWriteLock();
		}
	}
}
